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
//                       StampSource per slot keeps sampling single-writer,
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
#include <nexigen/core/shard_pool.hpp>
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
struct DroppedStats {
    std::uint64_t rate_limited{};   ///< Rejected by the throttle window (D9).
    std::uint64_t buffer_full{};    ///< Rejected by a full shard ring.
    std::uint64_t slot_exhausted{}; ///< Dropped: no free shard slot (D6).

    [[nodiscard]] constexpr std::uint64_t total() const noexcept {
        return rate_limited + buffer_full + slot_exhausted;
    }

    /// Cold-path diagnostic (mandate 9): per-cause counts + total.
    [[nodiscard]] std::string to_string() const {
        return "DroppedStats(rate_limited=" + std::to_string(rate_limited) +
               ", buffer_full=" + std::to_string(buffer_full) +
               ", slot_exhausted=" + std::to_string(slot_exhausted) +
               ", total=" + std::to_string(total()) + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const DroppedStats& stats) {
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
struct ThrottleConfig {
    bool enabled{false};
    std::uint64_t max_records_per_window{0}; ///< accepted budget per window
    std::uint64_t window_ns{0};              ///< window length, monotonic domain

    /// Cold-path diagnostic (mandate 9): full policy dump.
    [[nodiscard]] std::string to_string() const {
        return "ThrottleConfig(enabled=" + std::string(enabled ? "true" : "false") +
               ", max_records_per_window=" + std::to_string(max_records_per_window) +
               ", window_ns=" + std::to_string(window_ns) + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const ThrottleConfig& cfg) {
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

    /// Shard pool: one SPSC ring + accounting cell per thread, slot
    /// registration, exhaustion, and the consumer drain (core/
    /// shard_pool.hpp). Slot s's producer end is touched ONLY by its owner
    /// thread; the worker consumes all.
    core::ShardPool<Record, ShardCapacity, MaxThreads> pool_{};

    /// One stamp source PER SLOT (design D6): sample() writes last_ns_/anchors,
    /// so single-writer per slot keeps the clock race-free with zero atomics.
    std::array<chrono::StampSource, MaxThreads> stamps_{};

    /// Shared runtime threshold (relaxed cell, own cache line).
    LevelControl level_{};

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

    /// Shared cold-path body (design D9): build the "Dropped N record(s) ..."
    /// warning summary record from the given counts (spec: same text, all
    /// four causes). Returns false when the formatting failed (nothing is
    /// synthesized, cells keep their values). Never touches a ring: ownership
    /// of the ring push belongs to the caller's domain (owner `try_push` or
    /// direct sink delivery).
    [[nodiscard]] static bool compose_summary_record(std::uint64_t rl,
                                                     std::uint64_t bf,
                                                     std::uint64_t se,
                                                     std::uint64_t th,
                                                     const chrono::Timestamp stamp,
                                                     Record& rec) noexcept {
        char buf[256];
        const int n = std::snprintf(buf, sizeof buf,
                                    "Dropped %llu record(s) in the last window "
                                    "(rate-limited: %llu, buffer-full: %llu, "
                                    "slot-exhausted: %llu, throttled: %llu).",
                                    static_cast<unsigned long long>(rl) +
                                        static_cast<unsigned long long>(bf) +
                                        static_cast<unsigned long long>(se) +
                                        static_cast<unsigned long long>(th),
                                    static_cast<unsigned long long>(rl),
                                    static_cast<unsigned long long>(bf),
                                    static_cast<unsigned long long>(se),
                                    static_cast<unsigned long long>(th));
        if (n <= 0) {
            return false;
        }
        rec = Record{};
        rec.stamp = stamp;
        rec.level = LogLevel::Warning;
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
        return true;
    }

    /// Owner thread only: zero the summary-backed cells of one slot (the
    /// cells a summary's counts were composed from, except the global
    /// slot-exhaustion counter which stays cumulative).
    void reset_summary_cells(std::size_t slot) noexcept {
        core::ShardPoolState& st = pool_.state(slot);
        st.rate_limited.store(0, std::memory_order_relaxed);
        st.buffer_full.store(0, std::memory_order_relaxed);
        spam_state_[slot].throttled_count.store(0, std::memory_order_relaxed);
    }

    /// Owner thread only (its own slot): when drops are pending, pull them
    /// per-cause and try_push the composed summary into the OWNER'S OWN ring.
    /// Called on window transitions and before try_push when drops are
    /// pending. Counters reset only on push success (retry safety — spec
    /// logging/multi-producer "Failed summary push preserves counters").
    /// Includes all four causes.
    void emit_summary_if_needed(std::size_t slot, const chrono::Timestamp stamp) noexcept {
        core::ShardPoolState& st = pool_.state(slot);
        const std::uint64_t rl = st.rate_limited.load(std::memory_order_relaxed);
        const std::uint64_t bf = st.buffer_full.load(std::memory_order_relaxed);
        const std::uint64_t se = pool_.slot_exhausted();
        const std::uint64_t th = spam_state_[slot].throttled_count.load(std::memory_order_relaxed);
        if ((rl | bf | se | th) == 0ULL) {
            return;
        }
        Record rec{};
        if (!compose_summary_record(rl, bf, se, th, stamp, rec)) {
            return;
        }
        rec.thread_id = cached_thread_id();
        if (pool_.ring(slot).try_push(rec)) {
            reset_summary_cells(slot); // Reset only on success (retry safety).
        }
        // On failure: counters preserved; summary retried at next opportunity.
    }

    /// One relaxed atomic increment on the shard's own line (owner thread
    /// only). `fetch_add` — not `store(load+1)`: the consumer may reset the
    /// cell after reporting it (flush-time drop summary), so every increment
    /// must survive as an atomic RMW.
    static void bump(std::atomic<std::uint64_t>& counter) noexcept {
        counter.fetch_add(1U, std::memory_order_relaxed);
    }

public:
    /// Worker's own fresh monotonic stamp for synthesized summaries. The
    /// worker NEVER calls a per-slot `StampSource::sample()`: every
    /// `stamps_[i]` is producer-owned single-writer state (see
    /// stamp_source.hpp lines "written exclusively by the producer thread"),
    /// and a foreign sample() races the owner's `last_ns_` clamp — TSan
    /// reports exactly that on the multithreaded flush race test. The flush
    /// answer is a cold path: one `clock_gettime(CLOCK_MONOTONIC_RAW)` read
    /// (same clock domain as the producer stamps, so the summary stays in
    /// the same timeline). A clock failure degrades to a zero stamp.
    [[nodiscard]] static chrono::Timestamp worker_stamp() noexcept {
        const core::Result<chrono::Timestamp> now = chrono::Timestamp::now();
        return now ? *now : chrono::Timestamp{};
    }

    /// Consumer end only (the logging worker): flush-time drop summary
    /// (spec logging/multi-producer "Drop summaries are synthesized by the
    /// worker"). Scans all shards relaxed; on the first shard (or the global
    /// slot-exhaustion counter) with pending drops, builds the same summary
    /// record the owner would push (stamp read worker-side via
    /// `worker_stamp()`) and resets exactly the cells reported
    /// (rate-limited / buffer-full / throttled; the global slot-exhaustion
    /// counter stays cumulative). NEVER pushes into any ring — a ring slot
    /// is `try_push`-ed only by its owner (SPSC invariant), and never touches
    /// producer-owned stamp state — the caller delivers `out` directly to
    /// its sink. One summary per flush answer is enough; a later flush
    /// reports fresh drops only.
    /// Returns true with `out` filled after a summary was synthesized.
    [[nodiscard]] bool synthesize_flush_summary(Record& out) noexcept {
        const std::uint64_t global_se = pool_.slot_exhausted();
        for (std::size_t i = 0; i < MaxThreads; ++i) {
            const core::ShardPoolState& st = pool_.state(i);
            const std::uint64_t rl = st.rate_limited.load(std::memory_order_relaxed);
            const std::uint64_t bf = st.buffer_full.load(std::memory_order_relaxed);
            const std::uint64_t th = spam_state_[i].throttled_count.load(std::memory_order_relaxed);
            if ((rl | bf | th | global_se) != 0ULL) {
                Record rec{};
                if (!compose_summary_record(rl, bf, global_se, th,
                                            worker_stamp(), rec)) {
                    return false;
                }
                rec.thread_id = cached_thread_id();
                out = rec;
                reset_summary_cells(i);
                return true; // one summary per flush is enough
            }
        }
        return false;
    }
    using record_type = Record; ///< consumed by detail::AsyncWorker/Sink APIs

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

    /// Cold-path diagnostic/testing accessor for one shard slot. The live
    /// engine's overflow policy is fixed (DropNewest, counted per-cause in
    /// `ShardPoolState`); a per-shard OverwriteOldest mode does not exist —
    /// the unwired duplicate `logging::detail::SpscShardRing` was deleted
    /// (change fix-p1-races-process-correctness, analysis #7). Out-of-range
    /// indices fall back to slot 0 so tests cannot read out of bounds.
    [[nodiscard]] core::SpscRingBuffer<Record, ShardCapacity>& shard(
        std::size_t index) noexcept {
        return pool_.shard(index);
    }
    [[nodiscard]] const core::SpscRingBuffer<Record, ShardCapacity>& shard(
        std::size_t index) const noexcept {
        return pool_.ring(index < MaxThreads ? index : 0U);
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
    void configure_throttle(const ThrottleConfig& cfg) noexcept {
        policy_.enabled = cfg.enabled && cfg.max_records_per_window != 0ULL &&
                          cfg.window_ns != 0ULL;
        policy_.max_records_per_window = cfg.max_records_per_window;
        policy_.window_ns = cfg.window_ns;
    }

    /// Field-atomic runtime reconfiguration of anti-spam quotas (relaxed
    /// stores; hot path stays lock-free — see spam_protection.hpp / design
    /// D5 of change fix-p1-races-process-correctness).
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

        const std::int32_t slot = pool_.claim_slot();
        if (slot < 0) {
            return; // slot-exhausted drop already counted (never blocks)
        }

        const std::uint32_t tid = cached_thread_id();
        const chrono::Timestamp stamp = stamps_[static_cast<std::size_t>(slot)].sample();

        core::ShardPoolState& st = pool_.state(static_cast<std::size_t>(slot));
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

        const core::Status pushed =
            pool_.ring(static_cast<std::size_t>(slot)).try_push(rec);
        if (!pushed) {
            bump(st.buffer_full);
        }
    }

    /// Consumer end only (the internal worker via the async machinery):
    /// delegates to the pool's round-robin drain.
    [[nodiscard]] core::Status pop(Record& out) noexcept { return pool_.pop(out); }

    /// Third-thread snapshot diagnostics (never hot-path synchronization):
    /// relaxed aggregate over all shards + the shared exhaustion counter.
    [[nodiscard]] DroppedStats dropped() const noexcept {
        DroppedStats agg{};
        agg.rate_limited = pool_.rate_limited_total();
        agg.buffer_full = pool_.buffer_full_total();
        agg.slot_exhausted = pool_.slot_exhausted();
        return agg;
    }
    [[nodiscard]] std::size_t ring_size() const noexcept { return pool_.ring_size(); }
    [[nodiscard]] bool ring_empty() const noexcept { return pool_.ring_empty(); }
};

} // namespace detail
} // namespace nexigen::logging
