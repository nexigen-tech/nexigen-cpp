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
#include <nexigen/logging/detail/format_param.hpp>
#include <nexigen/logging/detail/level_control.hpp>
#include <nexigen/logging/detail/spam_protection.hpp>
#include <nexigen/logging/dropped_stats.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/log_record.hpp>
#include <nexigen/logging/throttle_config.hpp>

namespace nexigen::logging::detail {

/// @brief Resolve and cache this thread's Linux OS thread id.
///
/// The `SYS_gettid` syscall runs once per thread on first use; steady-state
/// calls read the `thread_local` value. TIDs are positive, so 0 doubles as
/// "unresolved" without a separate flag byte.
///
/// @return Positive Linux thread id for the calling thread.
/// @note Spec `logging/log-record`, requirement "Cached thread identity".
[[nodiscard]] inline std::uint32_t cached_thread_id() noexcept {
    thread_local std::uint32_t tid = 0U;
    if (tid == 0U) {
        tid = static_cast<std::uint32_t>(::syscall(SYS_gettid));
    }
    return tid;
}

/// @brief The hot-path pipeline shared by `ConsoleLogger` and `FileLogger`.
///
/// Composed (not inherited) by each concrete logger, which exposes the
/// `concepts::Logger` surface over it. Holds a fixed array of per-thread SPSC
/// shards plus all calibration, throttle, anti-spam and staging storage inline,
/// so an initialized producer never allocates.
///
/// Each producer thread owns exactly one shard slot; the single consumer (the
/// logger's worker thread) drains all shards. The first call per (thread,
/// producer) claims a slot with one relaxed `fetch_add` on the registration
/// counter plus a release publish; afterwards the `thread_local` cache resolves
/// the slot with no shared atomic touched. Threads beyond the pool are dropped
/// and counted (`slot_exhausted`) instead of blocking. The accepted path is
/// wait-free: no mutex, spin, yield, sleep or CAS loop. A full shard drops and
/// counts on the owner's cache line, so one thread's stall never slows another
/// thread's shard.
///
/// @tparam ShardCapacity Records per shard ring; must be a positive power of
///         two (enforced by `static_assert`).
/// @tparam MessageCapacity Inline argument-blob bytes per record.
/// @tparam MaxThreads Number of registrable producer slots (at least 1).
/// @note Spec `logging/multi-producer`, requirements "Pre-allocated per-thread
///       shard array", "Thread-local slot registration", and "Sharded drop
///       accounting without contention".
template <std::size_t ShardCapacity, std::size_t MessageCapacity, std::size_t MaxThreads = 32>
class Producer {
    static_assert(ShardCapacity >= 1 && (ShardCapacity & (ShardCapacity - 1)) == 0,
                  "shard capacity must be a positive power of two");
    static_assert(MaxThreads >= 1, "shard pool must hold at least one slot");

    /// @brief Record type stored in every shard and rendered by the sinks.
    using Record = LogRecord<MessageCapacity>;

    /// @brief Shard pool: one SPSC ring plus accounting cell per thread slot.
    ///
    /// Slot registration, exhaustion counting and the consumer drain live in
    /// `core/ShardPool` (core/shard_pool.hpp). Slot `s`'s producer end is
    /// touched ONLY by its owner thread; the worker consumes all shards.
    core::ShardPool<Record, ShardCapacity, MaxThreads> pool_{};

    /// @brief One stamp source per slot (design D6).
    ///
    /// `sample()` writes `last_ns_` and the re-anchoring state, so single-writer
    /// per slot keeps the clock race-free with zero atomics.
    std::array<chrono::StampSource, MaxThreads> stamps_{};

    /// @brief Shared runtime level threshold (relaxed cell, own cache line).
    LevelControl level_{};

    /// @brief Shared throttle policy read by producers on the emit path.
    ///
    /// Plain fields written once by `configure_throttle()` before the worker
    /// starts; the happens-before edge is thread creation.
    struct {
        /// Accepted records per window; 0 disables the budget.
        std::uint64_t max_records_per_window{0};
        /// Window length in nanoseconds; 0 disables the budget.
        std::uint64_t window_ns{0};
        /// Whether both quotas above are non-zero and throttle is active.
        bool enabled{false};
    } policy_{};

    /// @brief Anti-spam: repeat suppression plus token-bucket rate limiting.
    ///
    /// Default: both OFF (opt-in via `set_spam_protection_config()`).
    SpamProtection spam_{SpamProtectionConfig{
        .enable_repeat_suppression = false,
        .max_messages_per_sec_per_shard = 0,
    }};
    /// Per-shard anti-spam mutable state; index-aligned with the shard slots.
    SpamShardState spam_state_[MaxThreads];

    /// @brief Build a "Dropped N record(s)" warning summary record (cold path).
    ///
    /// Shared by the owner-side summary that is pushed into its own ring and by
    /// the worker-side flush summary that is delivered straight to a sink. All
    /// four drop causes use the same text; the record renders the text verbatim
    /// from its blob (`fmt` is empty, `render` is `render_text`). Never touches a
    /// ring: ownership of the push belongs to the caller's domain.
    ///
    /// @param rl Rate-limited drops since the previous summary.
    /// @param bf Buffer-full drops since the previous summary.
    /// @param se Cumulative global slot-exhaustion drops.
    /// @param th Token-bucket throttled drops since the previous summary.
    /// @param stamp Timestamp to publish with the summary.
    /// @param rec Output record; overwritten only on success.
    /// @return `true` when the record was composed; `false` when formatting
    ///         failed (nothing synthesized, caller keeps its counters).
    /// @note Spec `logging/multi-producer`, requirement "Drop summaries are
    ///       synthesized by the worker"; automatic summaries also satisfy
    ///       `logging/file-logger`, requirement "Automatic dropped-record
    ///       summary".
    [[nodiscard]] static bool compose_summary_record(std::uint64_t rl, std::uint64_t bf,
                                                     std::uint64_t se, std::uint64_t th,
                                                     const chrono::Timestamp stamp,
                                                     Record& rec) noexcept {
        char buf[256];
        const int n = std::snprintf(
            buf, sizeof buf,
            "Dropped %llu record(s) in the last window "
            "(rate-limited: %llu, buffer-full: %llu, "
            "slot-exhausted: %llu, throttled: %llu).",
            static_cast<unsigned long long>(rl) + static_cast<unsigned long long>(bf) +
                static_cast<unsigned long long>(se) + static_cast<unsigned long long>(th),
            static_cast<unsigned long long>(rl), static_cast<unsigned long long>(bf),
            static_cast<unsigned long long>(se), static_cast<unsigned long long>(th));
        if (n <= 0) {
            return false;
        }
        rec = Record{};
        rec.stamp = stamp;
        rec.level = core::LogLevel::Warning;
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

    /// @brief Zero the summary-backed drop cells of one shard (owner only).
    ///
    /// Clears the cells a composed summary's counts came from, except the global
    /// slot-exhaustion counter, which stays cumulative by specification.
    ///
    /// @param slot Shard slot whose per-cause cells are reset.
    /// @warning Owner thread only; `slot` must be the caller's own slot.
    /// @note Spec `logging/multi-producer`, requirement "Reported drop cells
    ///       reset after synthesis".
    void reset_summary_cells(std::size_t slot) noexcept {
        core::ShardPoolState& st = pool_.state(slot);
        st.rate_limited.store(0, std::memory_order_relaxed);
        st.buffer_full.store(0, std::memory_order_relaxed);
        spam_state_[slot].throttled_count.store(0, std::memory_order_relaxed);
    }

    /// @brief Push a drop summary into the owner's own ring when drops pend.
    ///
    /// Owner thread only (its own slot). When any cause is non-zero, pulls the
    /// per-cause counts, composes the summary record and `try_push`-es it into
    /// the OWNER'S OWN ring. Called on window transitions and before a user
    /// `try_push` when drops are pending. Counters reset only on push success,
    /// preserving retry safety.
    ///
    /// @param slot Caller's own shard slot.
    /// @param stamp Timestamp to publish with the summary.
    /// @warning Never call on a shard this thread does not own: an SPSC ring slot
    ///          may be pushed only by its owner.
    /// @note Spec `logging/multi-producer`, requirement "Reported drop cells
    ///       reset after synthesis" (the failed-push retry scenario) and "Drop
    ///       summaries are synthesized by the worker" for the flush path.
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

    /// @brief Commit one drop on the shard's own counter (owner only).
    ///
    /// One relaxed atomic increment on the shard's own cache line. `fetch_add`
    /// rather than `store(load + 1)`: the consumer may reset a reported cell
    /// (flush-time drop summary), so every increment must survive as an atomic
    /// RMW.
    ///
    /// @param counter Per-shard drop cell owned by the calling thread.
    /// @note Spec `logging/multi-producer`, requirement "Sharded drop accounting
    ///       without contention".
    static void bump(std::atomic<std::uint64_t>& counter) noexcept {
        counter.fetch_add(1U, std::memory_order_relaxed);
    }

public:
    /// @brief Worker's own fresh monotonic stamp for synthesized summaries.
    ///
    /// The worker NEVER calls a per-slot `StampSource::sample()`: every
    /// `stamps_[i]` is producer-owned single-writer state (see stamp_source.hpp
    /// lines "written exclusively by the producer thread"), and a foreign
    /// `sample()` races the owner's `last_ns_` clamp — TSan reports exactly that
    /// on the multithreaded flush race test. The flush answer is a cold path:
    /// one `clock_gettime(CLOCK_MONOTONIC_RAW)` read (the same clock domain as
    /// the producer stamps, so the summary stays in the same timeline). A clock
    /// failure degrades to a zero stamp.
    ///
    /// @return Fresh monotonic timestamp, or a zero stamp on clock failure.
    /// @note Spec `logging/log-record`, requirement "Monotonic nanosecond
    ///       stamping"; used by "Drop summaries are synthesized by the worker".
    [[nodiscard]] static chrono::Timestamp worker_stamp() noexcept {
        const core::Result<chrono::Timestamp> now = chrono::Timestamp::now();
        return now ? *now : chrono::Timestamp{};
    }

    /// @brief Consumer-only flush-time drop summary.
    ///
    /// Scans all shards with relaxed loads; on the first shard (or the global
    /// slot-exhaustion counter) with pending drops, builds the same summary
    /// record the owner would push — stamped worker-side via `worker_stamp()` —
    /// and resets exactly the cells it reported (rate-limited, buffer-full,
    /// throttled; the global slot-exhaustion counter stays cumulative). NEVER
    /// pushes into any ring: a shard ring is `try_push`-ed only by its owner
    /// (SPSC invariant), so the caller delivers `out` directly to its sink. One
    /// summary per flush answer is enough; a later flush reports fresh drops
    /// only.
    ///
    /// @param out Output record; filled only when a summary was synthesized.
    /// @return `true` when `out` holds a summary; `false` when nothing was
    ///         pending or composing the text failed.
    /// @note Spec `logging/multi-producer`, requirement "Drop summaries are
    ///       synthesized by the worker"; reset semantics are "Reported drop
    ///       cells reset after synthesis".
    [[nodiscard]] bool synthesize_flush_summary(Record& out) noexcept {
        const std::uint64_t global_se = pool_.slot_exhausted();
        for (std::size_t i = 0; i < MaxThreads; ++i) {
            const core::ShardPoolState& st = pool_.state(i);
            const std::uint64_t rl = st.rate_limited.load(std::memory_order_relaxed);
            const std::uint64_t bf = st.buffer_full.load(std::memory_order_relaxed);
            const std::uint64_t th = spam_state_[i].throttled_count.load(std::memory_order_relaxed);
            if ((rl | bf | th | global_se) != 0ULL) {
                Record rec{};
                if (!compose_summary_record(rl, bf, global_se, th, worker_stamp(), rec)) {
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
    /// @brief Record type consumed by the `detail::AsyncWorker`/Sink APIs.
    using record_type = Record; ///< consumed by detail::AsyncWorker/Sink APIs

    /// @brief Default-construct all inline shard and calibration storage.
    Producer() noexcept = default;
    /// @brief Copying is deleted: shard state is thread-owned and non-copyable.
    Producer(const Producer&) = delete;
    /// @brief Copy assignment is deleted: shard state is thread-owned.
    Producer& operator=(const Producer&) = delete;
    /// @brief Moving is deleted: thread-local slot caches key on this address.
    Producer(Producer&&) = delete;
    /// @brief Move assignment is deleted: thread-local slot caches key on this address.
    Producer& operator=(Producer&&) = delete;

    /// @brief Records per shard ring.
    ///
    /// @return `ShardCapacity`.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    /// @brief Number of registrable producer shard slots.
    ///
    /// @return `MaxThreads`.
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept { return MaxThreads; }
    /// @brief Inline argument-blob bytes per record.
    ///
    /// @return `MessageCapacity`.
    [[nodiscard]] static constexpr std::size_t message_capacity() noexcept {
        return MessageCapacity;
    }

    /// @brief Cold-path diagnostic accessor for one shard ring.
    ///
    /// The live engine's overflow policy is fixed (`DropNewest`, counted
    /// per-cause in `ShardPoolState`); a per-shard overwrite-oldest mode does not
    /// exist (the unwired duplicate `logging::detail::SpscShardRing` was
    /// deleted). Out-of-range indices fall back to slot 0 so tests cannot read
    /// out of bounds.
    ///
    /// @param index Shard slot index.
    /// @return Mutable ring for `index`.
    /// @note Spec `logging/multi-producer`, requirement "Pre-allocated per-thread
    ///       shard array".
    [[nodiscard]] core::SpscRingBuffer<Record, ShardCapacity>& shard(std::size_t index) noexcept {
        return pool_.shard(index);
    }
    /// @brief Const cold-path accessor for one shard ring.
    ///
    /// @param index Shard slot index; values at or above `MaxThreads` map to 0.
    /// @return Const ring for `index`.
    [[nodiscard]] const core::SpscRingBuffer<Record, ShardCapacity>&
    shard(std::size_t index) const noexcept {
        return pool_.ring(index < MaxThreads ? index : 0U);
    }

    /// @brief Cold path (logger initialize): calibrate the per-slot stamps.
    ///
    /// Any clock failure aborts initialization exactly like the old
    /// single-source contract; uncalibrated slots would still work through the
    /// `clock_gettime` fallback, just slower.
    ///
    /// @return OK when every slot's stamp source calibrated; the first
    ///         calibration error otherwise.
    /// @note Spec `logging/log-record`, requirement "Monotonic nanosecond
    ///       stamping" (calibrated cycle-counter fast path).
    [[nodiscard]] core::Status initialize() noexcept {
        for (auto& stamp : stamps_) {
            const core::Status calibrated = stamp.calibrate();
            if (!calibrated) {
                return calibrated;
            }
        }
        return {};
    }

    /// @brief Cold path (before the worker starts): install the throttle policy.
    ///
    /// Copies the public `ThrottleConfig` as-is and enables the budget only when
    /// both the maximum and the window are non-zero. Mirrors
    /// `nexigen::logging::ThrottleConfig` (throttle_config.hpp), which owns the
    /// public spelling of these parameters.
    ///
    /// @param cfg Windowed record budget; zero fields disable the throttle.
    /// @note Spec `logging/file-logger`, requirement "Hot-path rate limiting".
    void configure_throttle(const ThrottleConfig& cfg) noexcept {
        policy_.enabled =
            cfg.enabled && cfg.max_records_per_window != 0ULL && cfg.window_ns != 0ULL;
        policy_.max_records_per_window = cfg.max_records_per_window;
        policy_.window_ns = cfg.window_ns;
    }

    /// @brief Field-atomic runtime reconfiguration of anti-spam quotas.
    ///
    /// Relaxed stores only, so the hot path stays lock-free and producers never
    /// block (see spam_protection.hpp; design D5 of change
    /// fix-p1-races-process-correctness).
    ///
    /// @param cfg New quotas published field by field.
    /// @note Spec `logging/spam-protection`, requirement "Dynamic quota updates
    ///       are race-free".
    void set_spam_protection_config(const SpamProtectionConfig& cfg) noexcept {
        spam_.set_config(cfg);
    }

    /// @brief Publish a new level threshold through the shared relaxed cell.
    ///
    /// @param new_level New minimum level for accepted records.
    /// @note Spec `logging/logger-options`, requirement "Runtime reconfiguration
    ///       is wait-free".
    void set_level(core::LogLevel new_level) noexcept { level_.set_level(new_level); }
    /// @brief Read the published level threshold.
    ///
    /// @return Current minimum accepted level.
    [[nodiscard]] core::LogLevel level() const noexcept { return level_.level(); }

    /// @brief The hot path: filter, pack and enqueue one record.
    ///
    /// Order is fixed by the latency budget: one relaxed level load, thread-local
    /// shard resolution, cached thread id, shard-owned stamp, optional windowed
    /// throttle, fixed-offset argument packing plus the type-erased render
    /// callback, anti-spam checks, then a wait-free `try_push` on the caller's
    /// ring. Nothing formats on this thread.
    ///
    /// Returns void: every outcome (accepted, filtered, dropped) completes in
    /// bounded steps. Drops are counted per-cause in the shard's own state and
    /// surfaced through `dropped()`.
    ///
    /// @tparam Args Argument types; all must satisfy `Packable`. The pack size is
    ///         `static_assert`-checked against `MessageCapacity` at compile time.
    /// @param level Severity of the record.
    /// @param fmt Type-checked format string (`std::format_string`), carrying the
    ///        source location; only its program-static pointer is stored.
    /// @param args Values serialized into the record's inline blob. String views
    ///        are copied (safely truncated) at emit time, never referenced.
    /// @note Spec `logging/log-record`, requirements "Wait-free producer enqueue",
    ///       "Hot-path latency budget", "Zero runtime allocation in record
    ///       handling", and "Deferred type-erased argument formatting";
    ///       `logging/multi-producer`, requirements "Thread-local slot
    ///       registration" and "Slot exhaustion drops instead of blocking".
    template <class... Args>
        requires(Packable<std::remove_cvref_t<Args>> && ...)
    void emit(core::LogLevel level, detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        const core::LogLevel active = level_.level();
        if (active == core::LogLevel::Off || level < active) {
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
        [[maybe_unused]] constexpr auto offsets =
            arg_offsets<MessageCapacity, std::remove_cvref_t<Args>...>();
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
        // The FNV hash costs ~70 ns (serial dependent-multiply chain), so it is
        // computed ONLY when suppression is armed. The default is off, and a
        // disabled suppression never reads the hash (check_repeat short-circuits
        // on the same flag), so the hot path must not pay for it.
        if (spam_.repeat_suppression_enabled()) {
            const std::uint64_t msg_hash = message_hash(rec.file_name, rec.line, rec.fmt,
                                                        rec.arg_payload_.data(), rec.arg_length);
            if (!spam_.check_repeat(spam_st, msg_hash, stamp.nanoseconds())) {
                return; // suppressed duplicate
            }
        }

        const core::Status pushed = pool_.ring(static_cast<std::size_t>(slot)).try_push(rec);
        if (!pushed) {
            bump(st.buffer_full);
        }
    }

    /// @brief Consumer end: dequeue one record from the round-robin drain.
    ///
    /// Delegates to the pool's round-robin drain, which takes at most a bounded
    /// slice per shard per sweep so no shard can be starved.
    ///
    /// @param out Output record; filled only when a record was popped.
    /// @return OK when a record was popped; `ErrorCode::BufferEmpty` when every
    ///         registered shard is momentarily empty. Never returns a
    ///         partially filled record.
    /// @note Spec `logging/multi-producer`, requirement "Consumer drains every
    ///       active shard".
    [[nodiscard]] core::Status pop(Record& out) noexcept { return pool_.pop(out); }

    /// @brief Third-thread snapshot of per-cause drop totals.
    ///
    /// Relaxed aggregate over all shards plus the shared exhaustion counter. This
    /// is an observation, never synchronization: values may mix epochs, but each
    /// counter is a valid whole value.
    ///
    /// @return Aggregated rate-limited, buffer-full and slot-exhausted counts.
    /// @note Spec `logging/multi-producer`, requirement "Sharded drop accounting
    ///       without contention"; project mandate 9 for the `DroppedStats`
    ///       diagnostic surface.
    [[nodiscard]] DroppedStats dropped() const noexcept {
        DroppedStats agg{};
        agg.rate_limited = pool_.rate_limited_total();
        agg.buffer_full = pool_.buffer_full_total();
        agg.slot_exhausted = pool_.slot_exhausted();
        return agg;
    }
    /// @brief Relaxed snapshot of all pending records across shards.
    ///
    /// @return Sum of the shard ring sizes at observation time.
    /// @note Spec `logging/multi-producer`, requirement "Sharded drop accounting
    ///       without contention" (snapshot semantics).
    [[nodiscard]] std::size_t ring_size() const noexcept { return pool_.ring_size(); }
    /// @brief Relaxed snapshot: is every shard ring empty right now?
    ///
    /// @return `true` when no shard holds a pending record.
    [[nodiscard]] bool ring_empty() const noexcept { return pool_.ring_empty(); }
};

} // namespace nexigen::logging::detail
