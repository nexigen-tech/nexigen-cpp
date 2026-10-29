// nexigen::logging — shared hot-path producer pipeline with per-thread
// multi-producer sharding (change add-serialization-and-wait-free-logging,
// design D5/D6/D9; specs logging/multi-producer, logging/log-record).
//
// The entire accepted-record path for every asynchronous logger lives here,
// in this exact order, because the ≤ 20 ns budget is spent against it:
//
//   1. level filter   : one relaxed load + compare (≈ 1 ns, short-circuit
//                       path ENDS here — no stamp, no ring, no TLS work)
//   2. shard slot     : thread_local {owner, slot} lookup — a scan over ≤ 4
//                       entries on the thread's own TLS; the FIRST call per
//                       (thread, producer) additionally claims a slot with
//                       one relaxed fetch_add on a shared counter and
//                       publishes `active` (release). Steady state touches
//                       NO shared atomic at all.
//   3. thread id      : thread_local cached SYS_gettid, syscall on first use
//                       per thread only (D5)
//   4. stamp          : the SHARD's calibrated TSC source (design D6: one
//                       Stamp_source per slot keeps sampling single-writer,
//                       so re-anchoring/monotonic clamp stay race-free with
//                       no atomics)
//   5. (throttle)     : windowed budget in shard-owned plain state; enabled/
//                       policy shared, written only before the worker starts
//   6. argument pack  : fixed-offset memcpy of the compile-time pack into the
//                       record's inline blob + the type-erased render
//                       callback (NO std::format on this thread)
//   7. ring try_push  : the shard's own wait-free release store publishes
//                       the slot — the same single release as the old SPSC
//
// WAIT-FREE: no mutex, no spin, no yield, no sleep, no CAS anywhere on the
// accepted path. A full shard drops and counts on the OWNER's cache line;
// one thread's stall can never slow another thread's shard.
//
// MULTI-PRODUCER CONTRACT (spec logging/multi-producer): up to MaxThreads
// (32 by default) distinct producer threads may call emit() on ONE Producer;
// each thread owns exactly one SpscRingBuffer shard (its own SPSC pair with
// the single consumer = the logger's worker thread). Threads beyond the pool
// are dropped and counted (slot_exhausted). dropped()/ring_size() are
// relaxed cross-shard SNAPSHOTS — an observation, never synchronization —
// safe from third threads.
//
// Zero-allocation: shards, per-slot stamp sources, stats, staging — all
// inline fixed storage built with the object. initialize() calibrates the
// stamp sources (cold sleep ~2 ms each) before the first record.
//
// Result usage rule (see core/result.hpp): callers branch on has_value();
// never call .value().
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include <sys/syscall.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/logging/detail/arg_pack.hpp>
#include <nexigen/logging/detail/level_control.hpp>
#include <nexigen/logging/detail/spam_protection.hpp>
#include <nexigen/logging/format_with_location.hpp>
#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/log_record.hpp>

namespace nexigen::logging {

/// Snapshot of dropped-record accounting (spec logging/multi-producer: the
/// three drop causes are counted separately; third threads may read it
/// anytime as a cross-shard aggregate).
struct Dropped_stats {
    std::uint64_t rate_limited{};   ///< Rejected by the throttle window (D9).
    std::uint64_t buffer_full{};    ///< Rejected by a full shard ring.
    std::uint64_t slot_exhausted{}; ///< Dropped: no free shard slot (D6).

    [[nodiscard]] constexpr std::uint64_t total() const noexcept {
        return rate_limited + buffer_full + slot_exhausted;
    }

    /// Cold-path diagnostic (mandate 9): per-cause counts + total.
    [[nodiscard]] std::string to_string() const {
        return "Dropped_stats(rate_limited=" + std::to_string(rate_limited) +
               ", buffer_full=" + std::to_string(buffer_full) +
               ", slot_exhausted=" + std::to_string(slot_exhausted) +
               ", total=" + std::to_string(total()) + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const Dropped_stats& stats) {
    return stats.to_string();
}

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

    /// Cold-path diagnostic (mandate 9): full policy dump.
    [[nodiscard]] std::string to_string() const {
        return "Throttle_config(enabled=" + std::string(enabled ? "true" : "false") +
               ", max_records_per_window=" + std::to_string(max_records_per_window) +
               ", window_ns=" + std::to_string(window_ns) + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const Throttle_config& cfg) {
    return cfg.to_string();
}

/// The hot-path pipeline shared by ConsoleLogger/FileLogger: a fixed array of
/// per-thread SPSC shards (spec logging/multi-producer). Composed (not
/// inherited) by each concrete logger, which exposes the concepts::Logger
/// surface over it.
template <std::size_t ShardCapacity, std::size_t MessageCapacity, std::size_t MaxThreads = 32>
class Producer {
    static_assert(ShardCapacity >= 1 && (ShardCapacity & (ShardCapacity - 1)) == 0,
                  "shard capacity must be a positive power of two");
    static_assert(MaxThreads >= 1, "shard pool must hold at least one slot");

    using Record = LogRecord<MessageCapacity>;

    /// Shard pool: each element is a complete SPSC ring with its own
    /// cache-line-padded producer/consumer index cells. Only slot s's owner
    /// thread touches shards_[s]'s producer end; the worker consumes all.
    alignas(64) std::array<core::SpscRingBuffer<Record, ShardCapacity>, MaxThreads> shards_{};

    /// One stamp source PER SLOT (design D6): sample() writes last_ns_/anchors,
    /// so single-writer per slot keeps the clock race-free with zero atomics.
    std::array<chrono::Stamp_source, MaxThreads> stamps_{};

    /// Shared runtime threshold (relaxed cell, own cache line).
    Level_control level_{};

    /// Slot registration: next_slot is bumped with one relaxed fetch_add per
    /// (thread, producer) — never on the steady path; `active` is published
    /// with a release max-store so the consumer's acquire-load sees every
    /// slot it may drain. The max-store is a bounded CAS loop: cold path only.
    struct alignas(64) Registration_cell {
        std::atomic<std::uint32_t> next_slot{}; ///< relaxed fetch_add
        std::atomic<std::uint32_t> active{};    ///< release fetch_max (CAS loop)
        std::uint8_t pad_[56]{};
    } registration_{};

    /// Per-shard accounting + throttle window. The two counters are written
    /// ONLY by the slot's owner (relaxed store of prev+1 — never a contended
    /// RMW) and read relaxed by diagnostics; the window fields are plain
    /// single-writer state. One cache line per shard, no false sharing.
    struct alignas(64) Shard_state {
        std::atomic<std::uint64_t> rate_limited{}; ///< owner stores, others load
        std::atomic<std::uint64_t> buffer_full{};  ///< owner stores, others load
        std::uint64_t window_start_ns{};           ///< owner-only plain state
        std::uint64_t tokens_used{};               ///< owner-only plain state
        std::uint8_t pad_[32]{};
    };
    static_assert(sizeof(Shard_state) % 64 == 0, "shard state owns whole lines");
    Shard_state state_[MaxThreads];

    /// Pool-exhaustion counter: the ONLY shared atomic RMW on a drop path,
    /// reached only by misconfigured threads beyond MaxThreads.
    struct alignas(64) Exhausted_cell {
        std::atomic<std::uint64_t> slot_exhausted{};
        std::uint8_t pad_[56]{};
    } exhausted_{};

    /// Shared throttle policy: plain fields written once by configure_throttle
    /// (before the worker starts — happens-before via thread creation) and
    /// read by producers on the emit path.
    struct {
        std::uint64_t max_records_per_window{0};
        std::uint64_t window_ns{0};
        bool enabled{false};
    } policy_{};

    /// Anti-spam: repeat suppression + token-bucket rate limiting.
    /// Default: both OFF (opt-in via set_spam_protection_config).
    SpamProtection spam_{SpamProtectionConfig{
        .enable_repeat_suppression = false,
        .max_messages_per_sec_per_shard = 0,
    }};
    SpamShardState spam_state_[MaxThreads];

    /// Consumer-only round-robin cursor (the worker is the sole pop() caller).
    std::uint32_t drain_shard_{};
    std::uint32_t drain_shard_pops_{};

    /// First call per (thread, this): claim a slot; later calls read the
    /// thread_local cache. A tiny inline map keyed by producer address —
    /// real deployments run far fewer loggers per thread than entries.
    [[nodiscard]] std::int32_t this_thread_slot() noexcept {
        struct Entry {
            const void* owner{nullptr};
            std::int32_t slot{-1};
        };
        thread_local Entry tls[4]{}; // 4 distinct producers per thread
        for (const Entry& e : tls) {
            if (e.owner == this) {
                return e.slot; // steady path: TLS only, no shared atomics
            }
        }
        const std::uint32_t claimed =
            registration_.next_slot.fetch_add(1U, std::memory_order_relaxed);
        if (claimed >= MaxThreads) {
            exhausted_.slot_exhausted.fetch_add(1U, std::memory_order_relaxed);
            return -1; // dropped: no slot (spec "Slot exhaustion drops instead of blocking")
        }
        // Publish the high-water mark (release): the worker's acquire-load of
        // active() must not order *before* the slot exists to be drained.
        std::uint32_t cur = registration_.active.load(std::memory_order_relaxed);
        while (claimed + 1U > cur &&
               !registration_.active.compare_exchange_weak(
                   cur, claimed + 1U, std::memory_order_release, std::memory_order_relaxed)) {
            // bounded: cur strictly increases per retry, ≤ MaxThreads contenders
        }
        for (Entry& e : tls) {
            if (e.owner == nullptr) {
                e.owner = this;
                e.slot = static_cast<std::int32_t>(claimed);
                break;
            }
        }
        // TLS map full (5th distinct logger on this thread): the thread keeps
        // logging but re-registers per call — documented misconfiguration,
        // still wait-free, the pool drains into slot_exhausted past capacity.
        return static_cast<std::int32_t>(claimed);
    }

    /// D9 drop summary: on a window transition with pending drops, push a
    /// synthesized Warning record describing exact per-cause counts BEFORE the
    /// triggering record lands. Costs one extra bounded push on a rare path
    /// (O(windows), never O(drops)); counters reset only on push success, so
    /// a BufferFull summary re-arms and retries next transition. The summary
    /// consumes no token allowance (spec logging/file-logger).
    void emit_summary_if_needed(std::size_t slot, const chrono::Timestamp stamp) noexcept {
        Shard_state& st = state_[slot];
        const std::uint64_t rl = st.rate_limited.load(std::memory_order_relaxed);
        const std::uint64_t bf = st.buffer_full.load(std::memory_order_relaxed);
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
        Record rec{};
        rec.stamp = stamp;
        rec.level = LogLevel::Warning;
        rec.thread_id = cached_thread_id();
        rec.file_name = "<nexigen-logging>"; // internal synthesis (spec: diagnostic line)
        rec.function_name = "throttle_summary";
        rec.line = 0;
        rec.fmt = ""; // text lives in the blob (rendered verbatim)
        rec.render = &render_text;
        const std::size_t take = static_cast<std::size_t>(n) < MessageCapacity
                                    ? static_cast<std::size_t>(n)
                                    : MessageCapacity;
        std::memcpy(rec.arg_payload_.data(), buf, take);
        rec.arg_length = static_cast<std::uint16_t>(take);
        rec.flags = static_cast<std::uint8_t>(
            n > static_cast<int>(MessageCapacity) ? flag_args_truncated : 0U);
        if (shards_[slot].try_push(rec)) {
            st.rate_limited.store(0, std::memory_order_relaxed);
            st.buffer_full.store(0, std::memory_order_relaxed);
        }
    }

    /// One relaxed store bump on a shard's own line (owner thread only).
    static void bump(std::atomic<std::uint64_t>& counter) noexcept {
        counter.store(counter.load(std::memory_order_relaxed) + 1U, std::memory_order_relaxed);
    }

public:
    using record_type = Record; ///< consumed by detail::Async_worker/Sink APIs

    Producer() noexcept = default;
    Producer(const Producer&) = delete;
    Producer& operator=(const Producer&) = delete;
    Producer(Producer&&) = delete;
    Producer& operator=(Producer&&) = delete;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept { return MaxThreads; }
    [[nodiscard]] static constexpr std::size_t message_capacity() noexcept {
        return MessageCapacity;
    }

    /// Cold path (logger initialize): calibrate the per-slot stamp sources.
    /// Any clock failure aborts initialization (SystemError) exactly like the
    /// old single-source contract; uncalibrated slots would still work via
    /// the clock_gettime fallback, just slower.
    [[nodiscard]] core::Status initialize() noexcept {
        for (auto& stamp : stamps_) {
            const core::Status calibrated = stamp.calibrate();
            if (!calibrated) {
                return calibrated;
            }
        }
        return {};
    }

    /// Cold path (before the worker starts): install throttle policy.
    void configure_throttle(const Throttle_config& cfg) noexcept {
        policy_.enabled = cfg.enabled && cfg.max_records_per_window != 0ULL &&
                          cfg.window_ns != 0ULL;
        policy_.max_records_per_window = cfg.max_records_per_window;
        policy_.window_ns = cfg.window_ns;
    }

    /// Wait-free runtime reconfiguration of anti-spam quotas.
    void set_spam_protection_config(const SpamProtectionConfig& cfg) noexcept {
        spam_.set_config(cfg);
    }

    /// Level control forwards to the shared relaxed cell (design D2).
    void set_level(LogLevel new_level) noexcept { level_.set_level(new_level); }
    [[nodiscard]] LogLevel level() const noexcept { return level_.level(); }

    /// The hot path (spec logging/log-record, logging/multi-producer).
    /// Returns void: every outcome (accepted, filtered, dropped) completes in
    /// bounded steps; drops are counted per-cause in the shard's own state
    /// and surfaced through dropped(). Every Packable argument is serialized
    /// inline; NOTHING formats on this thread.
    template <class... Args>
        requires (Packable<std::remove_cvref_t<Args>> && ...)
    void emit(LogLevel level, detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        const LogLevel active = level_.level();
        if (active == LogLevel::Off || level < active) {
            return; // short-circuit: nothing else on this path (spec log-level)
        }

        const std::int32_t slot = this_thread_slot();
        if (slot < 0) {
            return; // slot-exhausted drop already counted (never blocks)
        }

        const std::uint32_t tid = cached_thread_id();
        const chrono::Timestamp stamp = stamps_[static_cast<std::size_t>(slot)].sample();

        Shard_state& st = state_[static_cast<std::size_t>(slot)];
        if (policy_.enabled) {
            // Windowed budget (design D9): shard-owned plain state, one
            // unsigned compare per steady call; transitions (rare) catch up
            // with one integer division and may synthesize the drop summary.
            const std::uint64_t ns = stamp.nanoseconds();
            if (st.window_start_ns != 0ULL && ns - st.window_start_ns >= policy_.window_ns) {
                const std::uint64_t skipped = (ns - st.window_start_ns) / policy_.window_ns;
                st.window_start_ns += skipped * policy_.window_ns;
                st.tokens_used = 0;
                emit_summary_if_needed(static_cast<std::size_t>(slot), stamp);
            } else if (st.window_start_ns == 0ULL) {
                st.window_start_ns = ns; // first call opens window 0
            }
            if (st.tokens_used >= policy_.max_records_per_window) {
                bump(st.rate_limited);
                return;
            }
            ++st.tokens_used;
        }

        // Fixed-offset argument pack (design D5): compile-time layout, one
        // bounded memcpy per slot, no formatting, no allocation.
        [[maybe_unused]] constexpr auto offsets = arg_offsets<MessageCapacity, std::remove_cvref_t<Args>...>();
        constexpr std::size_t total = arg_total<MessageCapacity, std::remove_cvref_t<Args>...>();
        static_assert(total <= MessageCapacity,
                      "format pack does not fit the inline blob: raise MessageCapacity");

        Record rec{};
        rec.level = level;
        rec.stamp = stamp;
        rec.thread_id = tid;
        rec.file_name = fmt.loc.file_name();
        rec.function_name = fmt.loc.function_name();
        rec.line = fmt.loc.line();
        rec.fmt = fmt.fmt.get().data(); // literal-backed NTBS (char[N] only)
        rec.render = &render_impl<MessageCapacity, std::remove_cvref_t<Args>...>;
        std::size_t index = 0;
        std::uint8_t flags = 0;
        ((pack_one<MessageCapacity>(rec.arg_payload_.data(), offsets[index++], args, flags)), ...);
        rec.arg_length = static_cast<std::uint16_t>(total);
        rec.flags = flags;

        // Anti-spam Level 2: token-bucket rate limiter (wait-free).
        SpamShardState& spam_st = spam_state_[static_cast<std::size_t>(slot)];
        if (!spam_.check_rate_limit(spam_st, level, stamp.nanoseconds())) {
            return; // throttled
        }

        // Anti-spam Level 1: repeat suppression (hash includes packed args).
        const std::uint64_t msg_hash = message_hash(
            rec.file_name, rec.line, rec.fmt,
            rec.arg_payload_.data(), rec.arg_length);
        if (!spam_.check_repeat(spam_st, msg_hash, stamp.nanoseconds())) {
            return; // suppressed duplicate
        }

        const core::Status pushed = shards_[static_cast<std::size_t>(slot)].try_push(rec);
        if (!pushed) {
            bump(st.buffer_full);
        }
    }

    /// Consumer end only (the internal worker via the async machinery).
    /// Round-robin across the registered shards with a 16-record slice per
    /// shard per sweep; a shard skipped at its cap is retried before any
    /// "empty" verdict, so a full sweep is genuinely momentary-empty ONLY
    /// when every registered shard has no record to give (the flush/drain
    /// barrier semantics of the old single-ring contract hold pool-wide).
    [[nodiscard]] core::Status pop(Record& out) noexcept {
        const std::uint32_t active = registration_.active.load(std::memory_order_acquire);
        bool deferred = false; // a shard was skipped at its 16-record slice
        for (std::uint32_t tried = 0U; tried < active; ++tried) {
            const std::uint32_t shard = drain_shard_;
            if (drain_shard_pops_ >= 16U) {
                deferred = true;
            } else if (shards_[shard].try_pop(out)) {
                ++drain_shard_pops_;
                return {};
            }
            drain_shard_ = active == 0U ? 0U : (shard + 1U) % active;
            drain_shard_pops_ = 0U;
        }
        if (deferred) {
            return pop(out); // one retry: budgets reset, real records surface
        }
        return std::unexpected(core::ErrorCode::BufferEmpty);
    }

    /// Third-thread snapshot diagnostics (never hot-path synchronization):
    /// relaxed aggregate over all shards + the shared exhaustion counter.
    [[nodiscard]] Dropped_stats dropped() const noexcept {
        Dropped_stats agg{};
        for (const Shard_state& st : state_) {
            agg.rate_limited += st.rate_limited.load(std::memory_order_relaxed);
            agg.buffer_full += st.buffer_full.load(std::memory_order_relaxed);
        }
        agg.slot_exhausted = exhausted_.slot_exhausted.load(std::memory_order_relaxed);
        return agg;
    }
    [[nodiscard]] std::size_t ring_size() const noexcept {
        std::size_t pending = 0;
        for (const auto& shard : shards_) {
            pending += shard.size();
        }
        return pending;
    }
    [[nodiscard]] bool ring_empty() const noexcept {
        for (const auto& shard : shards_) {
            if (!shard.is_empty()) {
                return false;
            }
        }
        return true;
    }
};

} // namespace detail
} // namespace nexigen::logging
