// nexigen::logging — shared hot-path producer pipeline (design.md D5/D7/D9).
//
// The entire accepted-record path for every asynchronous logger lives here,
// in this exact order, because the ≤ 20 ns budget is spent against it:
//
//   1. level filter   : one relaxed load + compare (≈ 1 ns, short-circuit
//                       path ENDS here — no stamp, no ring, no TLS work)
//   2. thread id      : thread_local cached SYS_gettid, syscall on first use
//                       per thread only (D5)
//   3. stamp          : calibrated TSC (x86-64) or clock_gettime fallback (D6)
//   4. (throttle)     : runtime-flagged windowed budget, plain producer-owned
//                       cells + one compare when off; drop summary is O(window
//                       transitions), never O(drops) (D9)
//   5. staging fill   : scalars + bounded memcpy of the message into the
//                       reused thread_local LogRecord
//   6. ring try_push  : the queue's own wait-free release store publishes
//                       the slot — this design adds NO new cross-thread
//                       edge on the producer path
//
// WAIT-FREE: no mutex, no spin, no yield, no sleep, no CAS anywhere. A full
// ring returns BufferFull immediately and bumps the (own-cache-line) drop
// counter; the consumer's state, stall, or rotation can never stall a call.
//
// SPSC CONTRACT (inherited from core::SpscRingBuffer): exactly ONE producer
// thread per Producer instance calls emit()/pop()-peer APIs; the internal
// worker thread is the sole consumer. Third threads may call level accessors,
// dropped() and ring_size() only — snapshots, never synchronization. The
// debug build option NEXIGEN_LOGGING_BIND_PRODUCER (risk R1) records the
// first producer tid and rejects foreign producers with InvalidArgument;
// production builds pay nothing for it (it compiles out entirely).
//
// Zero-allocation: everything (ring cells, stamp source, counters, staging)
// is inline fixed storage. calibrate() runs once during initialization.
//
// Result usage rule (see core/result.hpp): callers branch on has_value();
// never call .value().
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <source_location>
#include <string_view>
#include <utility>

#include <sys/syscall.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/core/timestamp.hpp>
#include <nexigen/logging/detail/level_control.hpp>
#include <nexigen/logging/detail/stamp_source.hpp>
#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/log_record.hpp>

namespace nexigen::logging {

/// Snapshot of dropped-record accounting (spec logging/file-logger: the two
/// drop causes are counted separately; third threads may read it anytime).
struct Dropped_stats {
    std::uint64_t rate_limited{}; ///< Rejected by the throttle window (D9).
    std::uint64_t buffer_full{};  ///< Rejected by the full ring.

    [[nodiscard]] constexpr std::uint64_t total() const noexcept {
        return rate_limited + buffer_full;
    }
};

namespace detail {

/// Per-thread cached Linux thread id (design D5): the SYS_gettid syscall runs
/// once per thread on first use; steady-state calls read the thread_local.
/// TIDs are positive, so 0 doubles as "unresolved" — no separate flag byte.
[[nodiscard]] inline std::uint32_t cached_thread_id() noexcept {
    thread_local std::uint32_t tid = 0U;
    if (tid == 0U) {
        tid = static_cast<std::uint32_t>(::syscall(SYS_gettid));
    }
    return tid;
}

/// Window throttle parameters (design D9; spec logging/file-logger).
/// Validation belongs to FileLogger::initialize; configure_throttle copies
/// as-is and only enables when max/window are non-zero.
struct Throttle_config {
    bool enabled{false};
    std::uint64_t max_records_per_window{0}; ///< accepted budget per window
    std::uint64_t window_ns{0};              ///< window length, monotonic domain
};

/// The hot-path pipeline shared by ConsoleLogger/FileLogger. Composed (not
/// inherited) by each concrete logger, which exposes the concepts::Logger
/// surface over it.
template <std::size_t Capacity, std::size_t MessageCapacity>
class Producer {
    using Record = LogRecord<MessageCapacity>;

    core::SpscRingBuffer<Record, Capacity> ring_{};
    Level_control level_{};
    Stamp_source stamp_{};

    /// Drop counters: their own cache line, written with relaxed fetch_add
    /// ONLY on drop paths (accepted calls never touch this line).
    struct alignas(64) Dropped_cell {
        std::atomic<std::uint64_t> rate_limited{};
        std::atomic<std::uint64_t> buffer_full{};
        std::uint8_t pad_[48]{};
    } dropped_{};

    /// Throttle window: producer-owned plain state (single-producer contract
    /// makes atomics unnecessary — the consumer never reads these), padded to
    /// its own line so neither the level cell nor the counters share traffic.
    struct alignas(64) Throttle_cell {
        std::uint64_t window_start_ns = 0;
        std::uint64_t tokens_used = 0;
        std::uint64_t max_records_per_window = 0;
        std::uint64_t window_ns = 0;
        bool enabled = false;
        std::uint8_t pad_[31]{};
    } throttle_{};

    static_assert(sizeof(Throttle_cell) == 64, "throttle window owns one cache line");

#ifdef NEXIGEN_LOGGING_BIND_PRODUCER
    /// Debug-build producer guard (risk R1): first emit() records the thread;
    /// foreign producers are rejected before touching the ring. Relaxed: the
    /// value is a policy check, not a synchronization edge.
    struct alignas(64) Bind_cell {
        std::atomic<std::uint32_t> producer_tid{};
        std::uint8_t pad_[60]{};
    } bind_{};
#endif

    /// Reused inline staging record (thread_local, trivial dtor): the message
    /// is copied here once, then try_push copies the whole record into the
    /// slot. Two bounded copies, zero construction cost per steady call.
    [[nodiscard]] static Record& staging() noexcept {
        thread_local Record rec{};
        return rec;
    }

    /// D9 drop summary: on a window transition with pending drops, push a
    /// synthesized Warning record describing exact per-cause counts BEFORE the
    /// triggering record lands. Costs one extra bounded push on a rare path
    /// (O(windows), never O(drops)); counters reset only on push success, so
    /// a BufferFull summary re-arms and retries next transition. The summary
    /// consumes no token allowance (spec logging/file-logger).
    void emit_summary_if_needed(core::Timestamp stamp) noexcept {
        const std::uint64_t rl = dropped_.rate_limited.load(std::memory_order_relaxed);
        const std::uint64_t bf = dropped_.buffer_full.load(std::memory_order_relaxed);
        if ((rl | bf) == 0ULL) {
            return;
        }
        char buf[192];
        const int n = std::snprintf(buf, sizeof buf,
                                    "Dropped %llu record(s) in the last window "
                                    "(rate-limited: %llu, buffer-full: %llu).",
                                    static_cast<unsigned long long>(rl + bf),
                                    static_cast<unsigned long long>(rl),
                                    static_cast<unsigned long long>(bf));
        if (n <= 0) {
            return;
        }
        Record& rec = staging();
        rec.flags = 0;
        rec.message_length = 0;
        rec.stamp = stamp;
        rec.level = LogLevel::Warning;
        rec.thread_id = cached_thread_id();
        rec.file_name = "<nexigen-logging>"; // internal synthesis (spec: diagnostic line)
        rec.function_name = "throttle_summary";
        rec.line = 0;
        rec.assign_message(std::string_view(buf, static_cast<std::size_t>(n)));
        if (ring_.try_push(rec)) {
            dropped_.rate_limited.store(0, std::memory_order_relaxed);
            dropped_.buffer_full.store(0, std::memory_order_relaxed);
        }
    }

public:
    using record_type = Record; ///< consumed by detail::Async_worker/Sink APIs

    Producer() noexcept = default;
    Producer(const Producer&) = delete;
    Producer& operator=(const Producer&) = delete;
    Producer(Producer&&) = delete;
    Producer& operator=(Producer&&) = delete;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    [[nodiscard]] static constexpr std::size_t message_capacity() noexcept {
        return MessageCapacity;
    }

    /// Cold path (logger initialize): arm the stamp fast source.
    [[nodiscard]] core::Status initialize() noexcept { return stamp_.calibrate(); }

    /// Cold path: install throttle policy (caller-validated; disabling is a
    /// single runtime flag flip — the machine compiles into a predictable
    /// not-taken branch when off, costing no syscall and no lock either way).
    void configure_throttle(const Throttle_config& cfg) noexcept {
        throttle_.enabled = cfg.enabled && cfg.max_records_per_window != 0ULL &&
                            cfg.window_ns != 0ULL;
        throttle_.max_records_per_window = cfg.max_records_per_window;
        throttle_.window_ns = cfg.window_ns;
        throttle_.window_start_ns = 0;
        throttle_.tokens_used = 0;
    }

    /// Level control forwards to the shared relaxed cell (design D2).
    void set_level(LogLevel new_level) noexcept { level_.set_level(new_level); }
    [[nodiscard]] LogLevel level() const noexcept { return level_.level(); }

    /// The hot path. Returns: {} accepted or filtered (success in both —
    /// filtering is normal operation), BufferFull when the ring rejected the
    /// push, RateLimited when the throttle window budget is exhausted,
    /// InvalidArgument only under NEXIGEN_LOGGING_BIND_PRODUCER for a foreign
    /// producer. Every outcome completes in bounded steps: no loop above the
    /// ring's single refresh, no lock, never a sleep/yield/spin.
    [[nodiscard]] core::Status emit(LogLevel level, std::source_location loc,
                                    std::string_view message) noexcept {
        const LogLevel active = level_.level();
        if (active == LogLevel::Off || level < active) {
            return {}; // short-circuit: nothing else on this path (spec log-level)
        }

        const std::uint32_t tid = cached_thread_id();

#ifdef NEXIGEN_LOGGING_BIND_PRODUCER
        const std::uint32_t bound = bind_.producer_tid.load(std::memory_order_relaxed);
        if (bound == 0U) {
            bind_.producer_tid.store(tid, std::memory_order_relaxed);
        } else if (bound != tid) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
#endif

        const core::Timestamp stamp = stamp_.sample();

        if (throttle_.enabled) {
            // Windowed budget (design D9): plain producer-owned state, one
            // unsigned compare per steady call; transitions (rare) catch up
            // with one integer division and may synthesize the drop summary.
            const std::uint64_t ns = stamp.nanoseconds();
            if (throttle_.window_start_ns != 0ULL &&
                ns - throttle_.window_start_ns >= throttle_.window_ns) {
                const std::uint64_t skipped = (ns - throttle_.window_start_ns) /
                                              throttle_.window_ns;
                throttle_.window_start_ns += skipped * throttle_.window_ns;
                throttle_.tokens_used = 0;
                emit_summary_if_needed(stamp);
            } else if (throttle_.window_start_ns == 0ULL) {
                throttle_.window_start_ns = ns; // first call opens window 0
            }
            if (throttle_.tokens_used >= throttle_.max_records_per_window) {
                dropped_.rate_limited.fetch_add(1, std::memory_order_relaxed);
                return std::unexpected(core::ErrorCode::RateLimited);
            }
            ++throttle_.tokens_used;
        }

        Record& rec = staging();
        rec.flags = 0;         // fresh: clears the truncation bit from reuse
        rec.message_length = 0;
        rec.level = level;
        rec.stamp = stamp;
        rec.thread_id = tid;
        rec.file_name = loc.file_name();
        rec.function_name = loc.function_name();
        rec.line = loc.line();
        rec.assign_message(message);

        const core::Status pushed = ring_.try_push(rec);
        if (!pushed) {
            dropped_.buffer_full.fetch_add(1, std::memory_order_relaxed);
        }
        return pushed;
    }

    /// Consumer end only (the internal worker via the async machinery).
    [[nodiscard]] core::Status pop(Record& out) noexcept { return ring_.try_pop(out); }

    /// Third-thread snapshot diagnostics (never hot-path synchronization).
    [[nodiscard]] Dropped_stats dropped() const noexcept {
        return Dropped_stats{
            dropped_.rate_limited.load(std::memory_order_relaxed),
            dropped_.buffer_full.load(std::memory_order_relaxed),
        };
    }
    [[nodiscard]] std::size_t ring_size() const noexcept { return ring_.size(); }
    [[nodiscard]] bool ring_empty() const noexcept { return ring_.is_empty(); }
};

} // namespace detail
} // namespace nexigen::logging
