// nexigen::tracing — the tracer provider: factory root, pre-allocated
// per-thread SPSC shard pool, runtime control, and the background consumer
// (design D5/D9/D10/D12; spec tracing/tracer-provider).
//
// TracerProvider<ShardCapacity = 1024, MaxThreads = 32> owns the ENTIRE
// tracing memory footprint inline (one allocation when the provider object
// itself is created — alignof == 64, so plain new already goes through
// C++17 aligned allocation and every member lands cache-aligned):
//   shards_    MaxThreads × SpscRingBuffer<SpanRecord, ShardCapacity>
//   stamps_    one single-writer StampSource PER SLOT (design D8: one rdtsc
//              + fixed-point multiply per sample; re-anchoring stays
//              race-free with zero atomics)
//   state_     per-shard owner-written drop/throttle cells (rate_limited,
//              buffer_full counters + window fields; never contended RMW)
//   registration_/exhausted_/control_ — slot claim, pool exhaustion, and the
//              ONE relaxed control byte read per span start (D9)
//   start_/flush_ — worker startup snapshot and the drain-barrier handshake
//   tracers_   the pre-allocated tracer pool (compile-time capacity, the
//              effective limit comes from the configuration)
// Default budget: 32 × 1024 × 320 B = 10 MiB shards + ~10 KiB of cells and
// pool — the object is ~10 MiB, so it lives in static storage or on the
// heap, NEVER on the stack (documented construction rule).
//
// HOT PATH (inline): span start touches one relaxed control load, the TLS
// slot map, its own shard's stamp source and state cell; end touches its
// shard's try_push and the TLS stack truncation. NO locks, NO CAS, NO
// allocation anywhere on the accepted path.
//
// COLD PATH (src/tracing/provider.cpp, explicitly instantiated for the
// default parameters): the consumer loop, flush handshake, registry under
// a std::mutex (control plane — mutexes are banned on hot paths only, D12).
//
// MEMORY ORDERS: the D13 table — control relaxed (atomicity only),
// registration fetch_add relaxed + release high-water publish, SPSC
// release/acquire inside the ring, drop counters relaxed, start_ and the
// flush handshake release/acquire, liveness release/acquire.
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <new>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/worker_thread.hpp>
#include <nexigen/core/shard_pool.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/core/worker_tuning.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/detail/active_span_stack.hpp>
#include <nexigen/tracing/detail/id_generator.hpp>
#include <nexigen/tracing/dropped_stats.hpp>
#include <nexigen/tracing/scoped_span.hpp>
#include <nexigen/tracing/span_context.hpp>
#include <nexigen/tracing/span_enums.hpp>
#include <nexigen/tracing/span_record.hpp>
#include <nexigen/tracing/tracer.hpp>
#include <nexigen/tracing/tracer_provider_config.hpp>

namespace nexigen::tracing {

namespace detail {

/// @brief Type-erased sink binding: plain function pointers, no virtual dispatch,
///        no RTTI (metrics D13 pattern).
///
/// Created once by the provider's templated constructor from a concrete
/// `TraceSink`, so the worker's drain loop never instantiates per-sink code.
/// The bound sink MUST outlive the provider (documented contract); every
/// pointer may be null, in which case that step is skipped.
///
/// @note Spec `tracing/tracer-concepts`, requirement "TraceSink structural
///       concept".
struct SinkBinding {
    /// @brief The sink object bound to every pointer below.
    void* self{nullptr};
    /// @brief Calls `sink.start()`; may be null (skipped).
    core::Status (*start)(void*) noexcept {nullptr};
    /// @brief Calls `sink.on_span(record)`; may be null (records discarded).
    core::Status (*on_span)(void*, const SpanRecord&) noexcept {nullptr};
    /// @brief Calls `sink.flush()`; may be null (nothing to synchronize).
    core::Status (*flush)(void*) noexcept {nullptr};
    /// @brief Calls `sink.stop()`; may be null (nothing to stop).
    void (*stop)(void*) noexcept {nullptr};
};

} // namespace detail

/// @brief The registry root and the only source of tracers.
///
/// Owns the whole tracing footprint inline: a `MaxThreads`-slot pool of
/// `ShardCapacity`-record SPSC shards, one single-writer stamp source per slot,
/// per-shard drop/throttle cells, the runtime control byte, the tracer pool and
/// the CPU-pinned background consumer. Construction performs no heap
/// allocation, and span start/end never allocate, lock or CAS on the accepted
/// path; only the control plane (`get_tracer`, `flush`, `initialize`,
/// destruction) reaches the registry mutex or the consumer handshake.
///
/// The object is roughly 10 MiB at the default parameters, so place it in
/// static storage or on the heap — never on the stack.
///
/// @tparam ShardCapacity Records per per-thread SPSC shard; must be a positive
///         power of two (pinned by `static_assert`).
/// @tparam MaxThreads Distinct producer slots; at least 1 and at most 127
///         because the span-record slot field is `std::int8_t` (pinned).
/// @warning The provider must outlive every sink it is bound to, and is neither
///          copyable nor movable; destroy it only after producer threads stop.
/// @note Spec `tracing/tracer-provider`, requirement "Pre-allocated provider
///       memory".
/// @note Spec `tracing/tracer-provider`, requirement "Provider and tracer
///       factory hierarchy".
template <std::size_t ShardCapacity = 1024, std::size_t MaxThreads = 32>
class alignas(64) TracerProvider {
    static_assert(ShardCapacity >= 1 && (ShardCapacity & (ShardCapacity - 1)) == 0,
                  "shard capacity must be a positive power of two");
    static_assert(MaxThreads >= 1, "shard pool must hold at least one slot");
    // Slot invariant (analysis #11; change fix-p1-races-process-correctness):
    // the claimed int32 slot is narrowed to the int8 span-record field
    // (`this_thread_slot()`/`std::int8_t slot`), so the pool width must fit a
    // signed byte — pinned at compile time instead of silently truncating
    // slots ≥ 128.
    static_assert(MaxThreads <= 127, "span-record slot field is int8 (MaxThreads must fit)");

public:
    /// @brief The record type this provider transports (`SpanRecord`).
    using Record = SpanRecord;
    /// @brief Alias of `Record` used by the sink-facing signatures.
    using record_type = SpanRecord;

    /// @brief Bind a sink and pre-allocate everything.
    ///
    /// The sink MUST outlive the provider. Construction itself performs NO heap
    /// allocation (all storage is inline in the object); the worker launches in
    /// `initialize()`. The concrete sink is erased into function pointers here,
    /// once, so no per-record virtual dispatch remains.
    ///
    /// @tparam Sink Concrete sink type, constrained to satisfy
    ///         `concepts::TraceSink` (all members `noexcept`, status-returning
    ///         delivery).
    /// @param config Provider configuration (worker spec, tracer limit, rate
    ///        budget, poll interval) copied by value into the provider.
    /// @param sink Sink to bind; must outlive this provider.
    /// @note Spec `tracing/tracer-concepts`, requirement "TraceSink structural
    ///       concept".
    template <concepts::TraceSink Sink>
    TracerProvider(const TracerProviderConfig& config, Sink& sink) noexcept
        : TracerProvider(config,
                         detail::SinkBinding{
                             static_cast<void*>(&sink),
                             [](void* self) noexcept -> core::Status {
                                 return static_cast<Sink*>(self)->start();
                             },
                             [](void* self, const SpanRecord& record) noexcept -> core::Status {
                                 return static_cast<Sink*>(self)->on_span(record);
                             },
                             [](void* self) noexcept -> core::Status {
                                 return static_cast<Sink*>(self)->flush();
                             },
                             [](void* self) noexcept { static_cast<Sink*>(self)->stop(); }}) {}

    /// @brief Copy construction is deleted: the provider owns worker and rings.
    TracerProvider(const TracerProvider&) = delete;
    /// @brief Copy assignment is deleted: the provider owns worker and rings.
    TracerProvider& operator=(const TracerProvider&) = delete;
    /// @brief Move construction is deleted: worker captures the object address.
    TracerProvider(TracerProvider&&) = delete;
    /// @brief Move assignment is deleted: worker captures the object address.
    TracerProvider& operator=(TracerProvider&&) = delete;

    /// @brief Cold path: calibrate every slot's stamp source, then launch the
    ///        background consumer.
    ///
    /// Idempotent (a second call succeeds without re-calibrating). MUST complete
    /// before recording starts — stamp calibration is single-writer setup, not
    /// an atomic transition. The worker self-applies its configured thread spec
    /// before the sink starts and before the first delivery.
    ///
    /// @return Success when calibrated and launched (or already launched);
    ///         otherwise the worker-thread validation error or the clock
    ///         calibration `SystemError`/status failure, with no worker left
    ///         running.
    /// @note Spec `tracing/tracer-provider`, requirement "CPU-pinned background
    ///       consumer", scenario "Required failure rolls back initialization".
    [[nodiscard]] core::Status initialize() noexcept;

    /// @brief Stop the worker, drain every accepted record to the sink, and leave
    ///        nothing accepted undelivered.
    ///
    /// A never-initialized provider drains inline on the destroying thread (no
    /// worker ever touched the sink — safe by construction). Handles report
    /// `NotInitialized` afterwards.
    ///
    /// @note Spec `tracing/tracer-provider`, requirement "Shutdown drain and
    ///       flush barrier", scenario "Destruction does not lose accepted
    ///       records".
    ~TracerProvider();

    /// @brief Get (or create) the tracer for a scope name.
    ///
    /// Repeated names return the same handle; a new name claims a pool slot
    /// (placement-constructed into the pre-allocated block) and its name is
    /// copied into that slot. Zero heap allocation. Control plane: the registry
    /// mutex, never the span path.
    ///
    /// @param scope_name Scope name; must fit `SpanRecord::name_capacity`
    ///        bytes (47) or the request is rejected.
    /// @return The stable handle on success; `InvalidArgument` for an oversized
    ///         name, `BufferFull` when the configured tracer pool is exhausted,
    ///         `NotInitialized` after provider destruction.
    /// @note Spec `tracing/tracer-provider`, requirement "Provider and tracer
    ///       factory hierarchy".
    [[nodiscard]] core::Result<Tracer<ShardCapacity, MaxThreads>*>
    get_tracer(std::string_view scope_name);

    /// @brief Control plane: enable or disable recording (D9).
    ///
    /// A relaxed read-modify-write of bit0 only: sampling bits are preserved,
    /// and subsequent span starts on every thread act on the new state without
    /// a lock or ordering handshake.
    ///
    /// @param enabled `true` to let spans record, `false` to short-circuit them.
    /// @note Spec `tracing/tracer-provider`, requirement "Dynamic runtime
    ///       control".
    void set_enabled(bool enabled) noexcept {
        const std::uint8_t current = control_.state.load(std::memory_order_relaxed);
        const std::uint8_t composed =
            static_cast<std::uint8_t>((current & 0xFEU) | (enabled ? 0x1U : 0x0U));
        control_.state.store(composed, std::memory_order_relaxed);
    }
    /// @brief Whether recording is currently enabled (relaxed snapshot).
    ///
    /// @return The enable bit as read once; a concurrent change may already
    ///         have moved on.
    [[nodiscard]] bool is_enabled() const noexcept {
        return (control_.state.load(std::memory_order_relaxed) & 0x1U) != 0U;
    }
    /// @brief Control plane: set the sampling decision (D9).
    ///
    /// A relaxed read-modify-write of bits 1-2 only; the enable bit is
    /// preserved. `Drop` makes subsequent span starts short-circuit to an
    /// inactive guard without any drop-counter increment.
    ///
    /// @param decision New decision recorded into the control byte.
    /// @note Spec `tracing/tracer-provider`, requirement "Dynamic runtime
    ///       control".
    void set_sampling(SamplingDecision decision) noexcept {
        const std::uint8_t current = control_.state.load(std::memory_order_relaxed);
        const unsigned sampled = static_cast<unsigned>(decision) << 1U;
        const std::uint8_t composed = static_cast<std::uint8_t>((current & 0xF9U) | sampled);
        control_.state.store(composed, std::memory_order_relaxed);
    }
    /// @brief The sampling decision currently configured (relaxed snapshot).
    ///
    /// @return Bits 1-2 of the control byte as a `SamplingDecision`.
    [[nodiscard]] SamplingDecision sampling() const noexcept {
        const std::uint8_t state = control_.state.load(std::memory_order_relaxed);
        return static_cast<SamplingDecision>((state >> 1U) & 0x3U);
    }

    /// @brief Drain barrier (control plane).
    ///
    /// Returns only after every span accepted before the call is sink-durable
    /// (drained AND flushed), and surfaces sink errors published by the worker.
    ///
    /// @return Success when the requested sequence was drained and flushed
    ///         cleanly; `NotInitialized` when no worker was started;
    ///         `SystemError` when the worker is gone or stuck (~10 s escape
    ///         hatch); otherwise the sink's flush error code.
    /// @note Spec `tracing/tracer-provider`, requirement "Shutdown drain and
    ///       flush barrier", scenario "Flush waits for durability".
    [[nodiscard]] core::Status flush() noexcept;

    /// @brief Third-thread snapshot diagnostics (never synchronization).
    ///
    /// Relaxed per-cause aggregate over all shards plus the shared exhaustion
    /// counter. Sampling-filtered spans never appear here: disable and `Drop`
    /// are decisions, not drops.
    ///
    /// @return Snapshot with `rate_limited`, `buffer_full` and `slot_exhausted`
    ///         counters read once, without ordering guarantees.
    /// @note Spec `tracing/tracer-provider`, requirement "Drop visibility".
    [[nodiscard]] DroppedStats dropped() const noexcept {
        DroppedStats aggregate{};
        aggregate.rate_limited = pool_.rate_limited_total();
        aggregate.buffer_full = pool_.buffer_full_total();
        aggregate.slot_exhausted = pool_.slot_exhausted();
        return aggregate;
    }

    /// @brief Snapshot pending-record count across all shards (diagnostics).
    ///
    /// @return Sum of the per-shard pending counts at read time; a relaxed
    ///         observation, not a synchronization point.
    [[nodiscard]] std::size_t ring_size() const noexcept { return pool_.ring_size(); }
    /// @brief Whether every shard is momentarily empty (diagnostics).
    ///
    /// @return `true` when no record is pending; producers may enqueue right
    ///         after the call returns.
    [[nodiscard]] bool ring_empty() const noexcept { return pool_.ring_empty(); }

    /// @brief Whether the consumer has published its startup snapshot.
    ///
    /// The worker publishes its post-pin mask and start-phase result after
    /// launch; this is pollable and never synchronizes with delivery.
    ///
    /// @return `true` once the worker has finished its startup phase.
    /// @note Spec `tracing/tracer-provider`, requirement "CPU-pinned background
    ///       consumer", scenario "Pin failures are visible without stopping the
    ///       pipeline".
    [[nodiscard]] bool worker_started() const noexcept { return worker_.ready(); }
    /// @brief First start-phase error of the worker (pin, scheduling or sink
    ///        start), or `Ok`.
    ///
    /// @return The startup error code captured before the drain loop began.
    [[nodiscard]] core::ErrorCode worker_start_code() const noexcept {
        return worker_.start_code();
    }
    /// @brief The CPU mask the worker actually applied after pinning.
    ///
    /// @return Post-pin affinity mask; zero/empty when the OS placement was
    ///         inherited or pinning failed.
    [[nodiscard]] std::uint64_t worker_mask() const noexcept { return worker_.mask(); }
    /// @brief The worker's thread-attribute application report.
    ///
    /// @return Report of which requested attributes were applied or failed;
    ///         best-effort failures remain visible without stopping delivery.
    [[nodiscard]] const core::ThreadApplyReport& worker_thread_report() const noexcept {
        return worker_.report();
    }

    /// @brief Compile-time shard capacity (`ShardCapacity`).
    ///
    /// @return Records per per-thread shard.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    /// @brief Compile-time slot count (`MaxThreads`).
    ///
    /// @return Maximum number of distinct producer slots.
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept { return MaxThreads; }

    /// @brief Cold-path diagnostic (mandate 9): configuration and live state.
    ///
    /// @return One-line summary carrying shard capacity, slot count, worker
    ///         spec, poll interval, tracer limit, enable/sampling state, worker
    ///         start state and the drop snapshot.
    /// @note Allocates; diagnostics/config/tests only, never a span path. Body in
    ///       `src/tracing/provider.cpp`.
    /// @note Spec `tracing/tracer-provider`, requirement "Provider serialization
    ///       surfaces".
    [[nodiscard]] std::string to_string() const;

private:
    friend class Tracer<ShardCapacity, MaxThreads>;
    friend class ScopedSpan<ShardCapacity, MaxThreads>;

    /// @brief Hidden common constructor: config + type-erased binding.
    ///
    /// Computes the rate-policy enable flag and the effective tracer limit
    /// (`min(config.max_tracers, tracer_pool_capacity)`); performed before any
    /// producer or worker exists, so the plain policy fields need no atomics.
    ///
    /// @param config Provider configuration copied into the object.
    /// @param binding Erased sink callbacks produced by the public constructor.
    TracerProvider(const TracerProviderConfig& config, detail::SinkBinding binding) noexcept;

    // -- hot-path machinery (inline; used by Tracer/ScopedSpan) -----------

    /// @brief Byte layout of `control_.state`: bit0 enabled, bits 1-2 the
    ///        `SamplingDecision`, the rest reserved (D9).
    static constexpr std::uint8_t control_enabled_bit = 0x1U;
    /// @brief Shift of the sampling decision inside `control_.state`.
    static constexpr std::uint8_t control_sampling_shift = 1U;

    /// @brief Claim (or recall) this thread's shard slot.
    ///
    /// TLS-cached on the steady path; the pool re-validates stale entries after
    /// a provider destroyed and re-created at the same address. A claim is cold
    /// (relaxed fetch_add plus release high-water publish).
    ///
    /// @return The 0-based slot index, or `-1` when the pool is exhausted
    ///         (already counted as `slot_exhausted`, never blocks).
    /// @note Spec `tracing/tracer-provider`, requirement "Per-thread shard
    ///       ownership".
    [[nodiscard]] std::int8_t this_thread_slot() noexcept {
        return static_cast<std::int8_t>(pool_.claim_slot());
    }

    /// @brief Per-shard accounting cell for the rate budget (friend access).
    ///
    /// @param slot Slot index previously returned by `this_thread_slot()`.
    /// @return Reference to the owner-written state cell for that slot.
    [[nodiscard]] core::ShardPoolState& state(std::size_t slot) noexcept {
        return pool_.state(slot);
    }

    /// @brief INLINE end-of-span path (design D8).
    ///
    /// End stamp, one SPSC `try_push` (a full ring bumps the owner-side
    /// `buffer_full` counter instead of blocking), then TLS stack truncation.
    /// No lock, no allocation, no indirect call; a stall of the consumer only
    /// ever shows up as a counted drop.
    ///
    /// @param record Fully filled record owned by the guard.
    /// @param slot Calling thread's shard slot, or negative when inactive.
    /// @param stack_index Frame index to truncate the TLS stack back to.
    /// @return Always `true`; the guard's `end()` reports span-ended semantics,
    ///         not push success.
    /// @note Spec `tracing/scoped-span`, requirement "Sub-20-nanosecond enqueue".
    /// @note Spec `tracing/scoped-span`, requirement "Disabled and dropped
    ///       short-circuit": `slot < 0` enqueues nothing and only truncates.
    bool finish_span(SpanRecord& record, std::int8_t slot, std::uint8_t stack_index) noexcept {
        if (slot >= 0) {
            const std::size_t index = static_cast<std::size_t>(slot);
            record.end = stamps_[index].sample();
            if (!pool_.ring(index).try_push(record)) {
                core::ShardPoolState& state = pool_.state(index);
                state.buffer_full.store(state.buffer_full.load(std::memory_order_relaxed) + 1U,
                                        std::memory_order_relaxed);
            }
        }
        detail::active_span_stack().truncate_to(stack_index);
        return true;
    }

    /// @brief Whether the configured span-rate budget applies.
    ///
    /// @return `true` when both the per-window span count and the window length
    ///         are non-zero.
    [[nodiscard]] bool rate_policy_enabled() const noexcept { return policy_.enabled; }
    /// @brief Spans accepted per rate window.
    ///
    /// @return Configured `max_spans_per_window` (0 when the budget is off).
    [[nodiscard]] std::uint64_t rate_max_spans() const noexcept {
        return policy_.max_spans_per_window;
    }
    /// @brief Length of one rate window in nanoseconds.
    ///
    /// @return Configured `window_ns` (0 when the budget is off).
    [[nodiscard]] std::uint64_t rate_window_ns() const noexcept { return policy_.window_ns; }

    // -- cold-path machinery (out-of-line; provider.cpp instantiates) ------

    /// @brief Consumer end: delegates to the pool's round-robin drain.
    ///
    /// Uses a 16-record per-shard per-sweep slice. Consumer thread only.
    ///
    /// @param out Record overwritten on success.
    /// @return Success when a record was produced; `BufferEmpty` when every
    ///         registered shard is momentarily empty.
    [[nodiscard]] core::Status pop(Record& out) noexcept { return pool_.pop(out); }

    /// @brief The background consumer body.
    ///
    /// Self-pins the thread (failures surface through `start_`), starts the
    /// sink, drains with idle backoff (64 failed sweeps, then ceiling-bounded
    /// sleeps), answers flush requests, and on shutdown drains on join and
    /// stops the sink.
    ///
    /// @param stop Stop token requested by `WorkerThread::stop()`.
    /// @note Spec `tracing/tracer-provider`, requirement "CPU-pinned background
    ///       consumer".
    void consume_loop(std::stop_token stop) noexcept;

    /// @brief Drain at most one bounded batch (64 records) into the sink.
    ///
    /// @param out Scratch record reused across the batch.
    /// @return Number of records delivered in this batch (`0` when empty).
    [[nodiscard]] std::size_t deliver_batch(Record& out) noexcept;

    /// @brief Answer a pending flush request while the rings were momentarily
    ///        empty.
    void answer_flush() noexcept;

    /// @brief Shutdown: drain everything still queued, publish the final
    ///        sequence, then stop the sink.
    ///
    /// @param out Scratch record reused across the drain.
    void shutdown_drain(Record& out) noexcept;

    /// @brief Flush publication: synchronize the sink, record the last sync error,
    ///        publish the drained sequence (release).
    ///
    /// @param sequence Flush-request sequence being acknowledged.
    void publish_sync(std::uint64_t sequence) noexcept;

    // -- state (design D5 layout) ------------------------------------------

    /// @brief Shard pool: one SPSC ring + accounting cell per thread, slot
    ///        registration, exhaustion, and the consumer drain
    ///        (`core/shard_pool.hpp`).
    ///
    /// Slot `s`'s producer end is touched ONLY by its owner thread; the single
    /// consumer drains all slots round-robin.
    core::ShardPool<SpanRecord, ShardCapacity, MaxThreads> pool_{};

    /// @brief One stamp source PER SLOT.
    ///
    /// Single-writer per slot keeps sampling race-free with zero atomics
    /// (design D6/D8); each producer samples only its own slot's source.
    std::array<chrono::StampSource, MaxThreads> stamps_{};

    /// @brief Runtime control (D9): bit0 enabled, bits 1-2 sampling, rest reserved.
    ///
    /// ONE relaxed load per span start; relaxed stores on change. The cell owns
    /// a whole cache line so control changes never false-share with producer
    /// traffic.
    struct alignas(64) ControlCell {
        /// @brief The composed enable/sampling byte.
        std::atomic<std::uint8_t> state{static_cast<std::uint8_t>(
            control_enabled_bit | (static_cast<std::uint8_t>(SamplingDecision::RecordAndSample)
                                   << control_sampling_shift))};
        std::uint8_t pad_[63]{}; ///< Pads the cell to one cache line.
    } control_{};

    /// @brief Flush handshake (D10) in its own cache line.
    ///
    /// The flushing thread `fetch_add`s a request; the worker publishes the
    /// drained sequence after syncing the sink; `sync_error` explains the last
    /// publication.
    struct alignas(64) FlushCell {
        /// @brief Monotonic count of flush requests (release by the caller).
        std::atomic<std::uint64_t> requested{};
        /// @brief Highest request sequence already drained and flushed.
        std::atomic<std::uint64_t> drained{};
        std::atomic<std::uint8_t> sync_error{}; ///< ErrorCode+1, 0 == clean
        std::uint8_t pad_[47]{};                ///< Pads the cell to one cache line.
    } flush_{};

    /// @brief Rate-limit policy: plain fields written once at construction,
    ///        read on the start path.
    ///
    /// Written before any producer or worker exists, so the start path reads
    /// them without atomics and without a data race.
    struct {
        std::uint64_t max_spans_per_window{0}; ///< Spans accepted per window.
        std::uint64_t window_ns{0};            ///< Window length in nanoseconds.
        bool enabled{false};                   ///< Budget active (both fields non-zero).
    } policy_{};

    TracerProviderConfig config_{}; ///< Configuration copy (cold reads).
    detail::SinkBinding binding_{}; ///< Erased sink callbacks.

    /// @brief Tracer pool: compile-time capacity; the EFFECTIVE limit is
    ///        `min(config.max_tracers, tracer_pool_capacity)`.
    ///
    /// The configuration sizes usage, the compile-time constant sizes storage.
    static constexpr std::size_t tracer_pool_capacity = 64;
    /// @brief One tracer-pool slot: inline scope-name storage plus occupancy.
    struct TracerSlot {
        char name[SpanRecord::name_capacity + 1]{}; ///< inline scope-name storage
        bool occupied{false};
        // The Tracer object is placement-constructed into tail padding on
        // claim (see get_tracer): provider pointer + name pointer, 16 bytes,
        // 8-aligned — fits the slot's alignment without a separate arena.
    };
    /// @brief Single inline block holding `tracer_pool_capacity` slot + tracer pairs.
    alignas(64)
        std::array<std::byte, tracer_pool_capacity *
                                  (sizeof(TracerSlot) +
                                   sizeof(Tracer<ShardCapacity, MaxThreads>))> tracer_storage_{};
    /// @brief Effective tracer limit taken from the configuration at construction.
    std::size_t tracer_limit_{0};

    /// @brief Registry lock (control plane only — D12).
    mutable std::mutex tracer_mutex_{};

    /// @brief Liveness flag: release-store in the destructor, acquire-load at every
    ///        span start — handle-after-death detection (`NotInitialized`).
    std::atomic<bool> alive_{true};

    /// @brief Worker body: applies the spec, starts the sink, runs the consumer.
    ///
    /// Captures `this`; `worker_` is the LAST member so it joins before the
    /// pool, stamps and tracer storage it references are destroyed.
    struct Body {
        TracerProvider* self{nullptr}; ///< Provider the worker runs for.

        /// @brief Worker start hook: calls the bound sink's `start()`.
        ///
        /// @return The sink's start status, or success when no sink is bound.
        [[nodiscard]] core::Status on_start() noexcept {
            return self->binding_.start != nullptr ? self->binding_.start(self->binding_.self)
                                                   : core::Status{};
        }
        /// @brief Worker run hook: enters the provider's consumer loop.
        ///
        /// @param stop Stop token supplied by `core::WorkerThread`.
        void run(std::stop_token stop) noexcept { self->consume_loop(stop); }
    };

    core::WorkerThread<Body> worker_{}; ///< CPU-pinned consumer (declared last).

    // Layout self-verification (design D5): the cells own whole cache lines.
    static_assert(sizeof(ControlCell) == 64);
    static_assert(sizeof(FlushCell) == 64);
};

// --- out-of-line cold members (definitions; provider.cpp explicitly
// instantiates the default parameters). --------------------------------------

template <std::size_t ShardCapacity, std::size_t MaxThreads>
TracerProvider<ShardCapacity, MaxThreads>::TracerProvider(const TracerProviderConfig& config,
                                                          detail::SinkBinding binding) noexcept
    : config_(config), binding_(binding) {
    policy_.enabled = config.max_spans_per_window != 0ULL && config.window_ns != 0ULL;
    policy_.max_spans_per_window = config.max_spans_per_window;
    policy_.window_ns = config.window_ns;
    tracer_limit_ = config.max_tracers < tracer_pool_capacity
                        ? static_cast<std::size_t>(config.max_tracers)
                        : tracer_pool_capacity;
}

template <std::size_t ShardCapacity, std::size_t MaxThreads>
core::Status TracerProvider<ShardCapacity, MaxThreads>::initialize() noexcept {
    if (worker_.joinable()) {
        return {}; // idempotent: already initialized
    }
    const core::Status valid_thread = config_.worker_thread.validate();
    if (!valid_thread) {
        return valid_thread;
    }
    for (auto& stamp : stamps_) {
        const core::Status calibrated = stamp.calibrate();
        if (!calibrated) {
            return calibrated;
        }
    }
    return worker_.start(config_.worker_thread, Body{this});
}

template <std::size_t ShardCapacity, std::size_t MaxThreads>
TracerProvider<ShardCapacity, MaxThreads>::~TracerProvider() {
    if (worker_.joinable()) {
        // consume_loop's shutdown path drains every accepted record, then
        // publishes + stops the sink, all while this object is still whole
        // (the worker member is declared last and joins before the pool dies).
        worker_.stop();
    } else if (binding_.on_span != nullptr &&
               !(config_.worker_thread.apply_mode() == core::ThreadApplyMode::Required &&
                 worker_.report().first_error() != core::ErrorCode::Ok)) {
        // Never initialized: no worker ever touched the sink — drain inline
        // on the destroying thread so accepted records are not lost (no
        // concurrent consumer exists by construction).
        static_cast<void>(binding_.start != nullptr ? binding_.start(binding_.self)
                                                    : core::Status{});
        Record record{};
        while (pop(record)) {
            static_cast<void>(binding_.on_span(binding_.self, record));
        }
        static_cast<void>(binding_.flush != nullptr ? binding_.flush(binding_.self)
                                                    : core::Status{});
        if (binding_.stop != nullptr) {
            binding_.stop(binding_.self);
        }
    }
    alive_.store(false, std::memory_order_release);
}

template <std::size_t ShardCapacity, std::size_t MaxThreads>
core::Result<Tracer<ShardCapacity, MaxThreads>*>
TracerProvider<ShardCapacity, MaxThreads>::get_tracer(std::string_view scope_name) {
    if (!alive_.load(std::memory_order_acquire)) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    if (scope_name.size() > SpanRecord::name_capacity) {
        return std::unexpected(core::ErrorCode::InvalidArgument); // bounded slot storage
    }
    const std::lock_guard<std::mutex> lock(tracer_mutex_);
    // One tracer slot = name storage + the Tracer object, carved out of the
    // single inline block.
    constexpr std::size_t slot_stride =
        (sizeof(TracerSlot) + alignof(Tracer<ShardCapacity, MaxThreads>) - 1U) /
            alignof(Tracer<ShardCapacity, MaxThreads>) *
            alignof(Tracer<ShardCapacity, MaxThreads>) +
        sizeof(Tracer<ShardCapacity, MaxThreads>);
    for (std::size_t i = 0; i < tracer_limit_; ++i) {
        auto* const slot = reinterpret_cast<TracerSlot*>(tracer_storage_.data() + i * slot_stride);
        if (slot->occupied && std::string_view(slot->name) == scope_name) {
            return reinterpret_cast<Tracer<ShardCapacity, MaxThreads>*>(
                reinterpret_cast<std::byte*>(slot) + sizeof(TracerSlot));
        }
    }
    for (std::size_t i = 0; i < tracer_limit_; ++i) {
        auto* const slot = reinterpret_cast<TracerSlot*>(tracer_storage_.data() + i * slot_stride);
        if (!slot->occupied) {
            for (std::size_t c = 0; c < scope_name.size(); ++c) {
                slot->name[c] = scope_name[c];
            }
            slot->name[scope_name.size()] = '\0';
            slot->occupied = true;
            auto* const tracer = new (reinterpret_cast<std::byte*>(slot) + sizeof(TracerSlot))
                Tracer<ShardCapacity, MaxThreads>(this, slot->name);
            return tracer;
        }
    }
    return std::unexpected(core::ErrorCode::BufferFull);
}

template <std::size_t ShardCapacity, std::size_t MaxThreads>
std::size_t TracerProvider<ShardCapacity, MaxThreads>::deliver_batch(Record& out) noexcept {
    std::size_t delivered = 0;
    while (delivered < 64 && pop(out)) { // bounded batch
        if (binding_.on_span != nullptr) {
            // Delivery failures never stop the drain: the consumer keeps
            // serving every shard; a failing sink surfaces its own state
            // through flush() (sync_error) and its diagnostics.
            static_cast<void>(binding_.on_span(binding_.self, out));
        }
        ++delivered;
    }
    return delivered;
}

template <std::size_t ShardCapacity, std::size_t MaxThreads>
void TracerProvider<ShardCapacity, MaxThreads>::answer_flush() noexcept {
    const std::uint64_t request = flush_.requested.load(std::memory_order_acquire);
    if (request > flush_.drained.load(std::memory_order_relaxed)) {
        publish_sync(request);
    }
}

template <std::size_t ShardCapacity, std::size_t MaxThreads>
void TracerProvider<ShardCapacity, MaxThreads>::shutdown_drain(Record& out) noexcept {
    // Drain what is still queued, synchronize once, publish, then stop the
    // sink (spec "Destruction does not lose accepted records").
    while (pop(out)) {
        if (binding_.on_span != nullptr) {
            static_cast<void>(binding_.on_span(binding_.self, out));
        }
    }
    publish_sync(flush_.requested.load(std::memory_order_relaxed));
    if (binding_.stop != nullptr) {
        binding_.stop(binding_.self);
    }
}

template <std::size_t ShardCapacity, std::size_t MaxThreads>
void TracerProvider<ShardCapacity, MaxThreads>::consume_loop(std::stop_token stop) noexcept {
    // Idle policy shared with the logging consumer (core/worker_tuning.hpp).
    const core::WorkerTuning tuning{.poll_interval_us = config_.worker_poll_interval_us};
    const std::uint64_t poll_max_us = core::WorkerTuning::clamp_poll_us(tuning.poll_interval_us);
    core::WorkerTuning::apply_timer_slack(poll_max_us);

    Record record{};
    std::uint64_t idle = 0;
    while (!stop.stop_requested()) {
        if (deliver_batch(record) != 0) {
            idle = 0;
            continue;
        }
        // Momentarily empty: answer any flush request before going idle.
        answer_flush();
        if (++idle <= tuning.spin_budget) {
            continue;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(
            core::WorkerTuning::backoff_us(idle - tuning.spin_budget, poll_max_us)));
    }

    shutdown_drain(record);
}

template <std::size_t ShardCapacity, std::size_t MaxThreads>
void TracerProvider<ShardCapacity, MaxThreads>::publish_sync(std::uint64_t sequence) noexcept {
    core::Status sync{};
    if (binding_.flush != nullptr) {
        sync = binding_.flush(binding_.self);
    }
    flush_.sync_error.store(
        sync ? std::uint8_t{0}
             : static_cast<std::uint8_t>(static_cast<std::uint8_t>(sync.error()) + 1U),
        std::memory_order_relaxed);
    flush_.drained.store(sequence, std::memory_order_release);
}

template <std::size_t ShardCapacity, std::size_t MaxThreads>
core::Status TracerProvider<ShardCapacity, MaxThreads>::flush() noexcept {
    if (!worker_.joinable()) {
        return std::unexpected(core::ErrorCode::NotInitialized); // not initialized
    }
    const std::uint64_t sequence = flush_.requested.fetch_add(1U, std::memory_order_release) + 1U;
    for (std::uint64_t spins = 0; flush_.drained.load(std::memory_order_acquire) < sequence;
         ++spins) {
        if (spins >= 50'000) { // worker gone/stuck: escape hatch (~10 s)
            return std::unexpected(core::ErrorCode::SystemError);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    const std::uint8_t error = flush_.sync_error.load(std::memory_order_acquire);
    if (error != 0U) {
        return std::unexpected(static_cast<core::ErrorCode>(error - 1U));
    }
    return {};
}

template <std::size_t ShardCapacity, std::size_t MaxThreads>
std::string TracerProvider<ShardCapacity, MaxThreads>::to_string() const {
    std::string out = "TracerProvider(shard_capacity=";
    out += std::to_string(ShardCapacity);
    out += ", max_threads=" + std::to_string(MaxThreads);
    out += ", worker_thread=" + config_.worker_thread.to_string();
    out += ", worker_poll_interval_us=" + std::to_string(config_.worker_poll_interval_us);
    out += ", tracer_limit=" + std::to_string(tracer_limit_);
    out += ", enabled=" + std::string(is_enabled() ? "true" : "false");
    out += ", sampling=" + std::string(tracing::to_string(sampling()));
    out += ", worker_started=" + std::string(worker_started() ? "true" : "false");
    out += ", dropped=" + dropped().to_string();
    out += ")";
    return out;
}

} // namespace nexigen::tracing

/// @brief `std::formatter` for `TracerProvider` (mandate 11).
///
/// The provider formats as its diagnostic string, matching `to_string()`.
///
/// @tparam SC Shard capacity of the provider.
/// @tparam MT Shard-slot count of the provider.
template <std::size_t SC, std::size_t MT>
struct std::formatter<nexigen::tracing::TracerProvider<SC, MT>> : std::formatter<std::string_view> {
    /// @brief Render one provider as its diagnostic text.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param provider Provider to render.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::TracerProvider<SC, MT>& provider, Ctx& ctx) const {
        const std::string text = provider.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
