// nexigen::tracing — the tracer provider: factory root, pre-allocated
// per-thread SPSC shard pool, runtime control, and the background consumer
// (design D5/D9/D10/D12; spec tracing/tracer-provider).
//
// TracerProvider<ShardCapacity = 1024, Max_threads = 32> owns the ENTIRE
// tracing memory footprint inline (one allocation when the provider object
// itself is created — alignof == 64, so plain new already goes through
// C++17 aligned allocation and every member lands cache-aligned):
//   shards_    Max_threads × SpscRingBuffer<SpanRecord, ShardCapacity>
//   stamps_    one single-writer Stamp_source PER SLOT (design D8: one rdtsc
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
#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/detail/active_span_stack.hpp>
#include <nexigen/tracing/detail/affinity.hpp>
#include <nexigen/tracing/detail/id_generator.hpp>
#include <nexigen/tracing/scoped_span.hpp>
#include <nexigen/tracing/span_context.hpp>
#include <nexigen/tracing/span_enums.hpp>
#include <nexigen/tracing/span_record.hpp>
#include <nexigen/tracing/tracer.hpp>

namespace nexigen::tracing {

/// Provider configuration (value type, full serialization trio; bodies in
/// src/tracing/record_format.cpp): CPU affinity of the background consumer,
/// the effective tracer pool size, and the optional span-rate budget (two
/// zeros = unlimited).
struct TracerProvider_config {
    std::uint64_t core_mask{0};            ///< worker pin (0 = OS placement).
    std::uint32_t max_tracers{16};         ///< effective pool limit (≤ 64).
    std::uint64_t max_spans_per_window{0}; ///< 0 = no rate limiting.
    std::uint64_t window_ns{0};            ///< 0 = no rate limiting.

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const TracerProvider_config& config);

/// ADL JSON (cold path, full round-trip under JSON_NOEXCEPTION=1).
void to_json(nlohmann::json& j, const TracerProvider_config& config);
void from_json(const nlohmann::json& j, TracerProvider_config& out) noexcept;

/// Snapshot of dropped-span accounting (spec "Drop visibility"): the three
/// drop causes counted separately; readable from any thread at any time as
/// a relaxed aggregate observation, never as synchronization. Sampling-
/// filtered spans are NOT counted (a decision is not a failure).
struct Dropped_stats {
    std::uint64_t rate_limited{};   ///< Rejected by the span-rate budget.
    std::uint64_t buffer_full{};    ///< Rejected by a full shard ring.
    std::uint64_t slot_exhausted{}; ///< Dropped: no free shard slot.

    [[nodiscard]] constexpr std::uint64_t total() const noexcept {
        return rate_limited + buffer_full + slot_exhausted;
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const Dropped_stats& stats);

/// ADL JSON (cold path, full round-trip — the snapshot is a value type).
void to_json(nlohmann::json& j, const Dropped_stats& stats);
void from_json(const nlohmann::json& j, Dropped_stats& out) noexcept;

namespace detail {

/// Type-erased sink binding: plain function pointers, no virtual dispatch,
/// no RTTI (metrics D13 pattern). Created once by the provider's templated
/// constructor; the sink MUST outlive the provider (documented contract).
struct Sink_binding {
    void* self{nullptr};
    core::Status (*start)(void*) noexcept{nullptr};
    core::Status (*on_span)(void*, const SpanRecord&) noexcept{nullptr};
    core::Status (*flush)(void*) noexcept{nullptr};
    void (*stop)(void*) noexcept{nullptr};
};

} // namespace detail

/// The registry root and the only source of tracers.
template <std::size_t ShardCapacity = 1024, std::size_t Max_threads = 32>
class alignas(64) TracerProvider {
    static_assert(ShardCapacity >= 1 && (ShardCapacity & (ShardCapacity - 1)) == 0,
                  "shard capacity must be a positive power of two");
    static_assert(Max_threads >= 1, "shard pool must hold at least one slot");

public:
    using Record = SpanRecord;
    using record_type = SpanRecord;

    /// Bind a sink and pre-allocate everything. The sink MUST outlive the
    /// provider. Construction itself performs NO heap allocation (all
    /// storage is inline in the object); the worker launches in initialize().
    template <concepts::TraceSink Sink>
    TracerProvider(const TracerProvider_config& config, Sink& sink) noexcept
        : TracerProvider(config,
                         detail::Sink_binding{
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
                             [](void* self) noexcept {
                                 static_cast<Sink*>(self)->stop();
                             }}) {}

    TracerProvider(const TracerProvider&) = delete;
    TracerProvider& operator=(const TracerProvider&) = delete;
    TracerProvider(TracerProvider&&) = delete;
    TracerProvider& operator=(TracerProvider&&) = delete;

    /// Cold path: calibrate every slot's stamp source, then launch the
    /// background consumer. Idempotent (a second call succeeds without
    /// re-calibrating). MUST complete before recording starts — stamp
    /// calibration is single-writer setup, not an atomic transition. Returns
    /// SystemError when the clock calibration fails.
    [[nodiscard]] core::Status initialize() noexcept;

    /// Stop the worker, drain every accepted record to the sink, and leave
    /// nothing accepted undelivered (spec "Destruction does not lose accepted
    /// records"). A never-initialized provider drains inline on the
    /// destroying thread (no worker ever touched the sink — safe by
    /// construction). Handles report NotInitialized afterwards.
    ~TracerProvider();

    /// Get (or create) the tracer for a scope name: repeated names return
    /// the same handle, new names claim a pool slot (BufferFull when the
    /// configured pool is exhausted). Zero heap allocation. Control plane:
    /// the registry mutex (D12).
    [[nodiscard]] core::Result<Tracer<ShardCapacity, Max_threads>*>
    get_tracer(std::string_view scope_name);

    /// Control plane: enable/disable and sampling decision (D9) — relaxed
    /// stores; subsequent span starts on every thread act on the new state.
    void set_enabled(bool enabled) noexcept {
        const std::uint8_t current = control_.state.load(std::memory_order_relaxed);
        const std::uint8_t composed = static_cast<std::uint8_t>(
            (current & 0xFEU) | (enabled ? 0x1U : 0x0U));
        control_.state.store(composed, std::memory_order_relaxed);
    }
    [[nodiscard]] bool is_enabled() const noexcept {
        return (control_.state.load(std::memory_order_relaxed) & 0x1U) != 0U;
    }
    void set_sampling(SamplingDecision decision) noexcept {
        const std::uint8_t current = control_.state.load(std::memory_order_relaxed);
        const unsigned sampled = static_cast<unsigned>(decision) << 1U;
        const std::uint8_t composed =
            static_cast<std::uint8_t>((current & 0xF9U) | sampled);
        control_.state.store(composed, std::memory_order_relaxed);
    }
    [[nodiscard]] SamplingDecision sampling() const noexcept {
        const std::uint8_t state = control_.state.load(std::memory_order_relaxed);
        return static_cast<SamplingDecision>((state >> 1U) & 0x3U);
    }

    /// Drain barrier (control plane; spec "Flush waits for durability"):
    /// returns only after every span accepted before the call is sink-durable
    /// (drained AND flushed), surfacing sink errors.
    [[nodiscard]] core::Status flush() noexcept;

    /// Third-thread snapshot diagnostics (never synchronization): the
    /// relaxed per-cause aggregate over all shards + the shared exhaustion
    /// counter. Sampling-filtered spans never appear here.
    [[nodiscard]] Dropped_stats dropped() const noexcept {
        Dropped_stats aggregate{};
        for (const Shard_state& state : state_) {
            aggregate.rate_limited += state.rate_limited.load(std::memory_order_relaxed);
            aggregate.buffer_full += state.buffer_full.load(std::memory_order_relaxed);
        }
        aggregate.slot_exhausted = exhausted_.slot_exhausted.load(std::memory_order_relaxed);
        return aggregate;
    }

    /// Snapshot pending-record count across all shards (diagnostics).
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

    /// Worker startup snapshot (pollable; spec "Worker affinity matches the
    /// configured mask" / "Pin failures are visible without stopping the
    /// pipeline"): the worker publishes its post-pin mask and the first
    /// start-phase error (pin failure or sink start failure) after launch.
    [[nodiscard]] bool worker_started() const noexcept {
        return start_.started.load(std::memory_order_acquire);
    }
    [[nodiscard]] core::ErrorCode worker_start_code() const noexcept {
        return static_cast<core::ErrorCode>(start_.code.load(std::memory_order_relaxed));
    }
    [[nodiscard]] std::uint64_t worker_mask() const noexcept {
        return start_.mask.load(std::memory_order_relaxed);
    }

    /// Compile-time capacities.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept {
        return Max_threads;
    }

    /// Cold-path diagnostic (mandate 9): configuration and live state.
    [[nodiscard]] std::string to_string() const;

private:
    friend class Tracer<ShardCapacity, Max_threads>;
    friend class ScopedSpan<ShardCapacity, Max_threads>;

    /// Hidden common constructor: config + type-erased binding.
    TracerProvider(const TracerProvider_config& config,
                   detail::Sink_binding binding) noexcept;

    // -- hot-path machinery (inline; used by Tracer/ScopedSpan) -----------

    /// Byte layout of control_.state: bit0 enabled, bits 1-2 the
    /// SamplingDecision, the rest reserved (D9).
    static constexpr std::uint8_t control_enabled_bit = 0x1U;
    static constexpr std::uint8_t control_sampling_shift = 1U;

    /// First call per (thread, this): claim a slot; later calls read the
    /// thread_local cache. A 4-entry inline map keyed by provider address
    /// (logging Producer pattern). Beyond Max_threads distinct threads: the
    /// shared exhaustion counter bumps (the ONLY shared atomic RMW on a drop
    /// path) and the caller drops.
    [[nodiscard]] std::int8_t this_thread_slot() noexcept {
        struct Entry {
            const void* owner{nullptr};
            std::int32_t slot{-1};
        };
        thread_local Entry tls[4]{}; // 4 distinct providers per thread
        for (const Entry& entry : tls) {
            if (entry.owner == this) {
                return static_cast<std::int8_t>(entry.slot); // TLS only
            }
        }
        const std::uint32_t claimed =
            registration_.next_slot.fetch_add(1U, std::memory_order_relaxed);
        if (claimed >= Max_threads) {
            exhausted_.slot_exhausted.fetch_add(1U, std::memory_order_relaxed);
            return -1; // dropped: no slot (never blocks)
        }
        // Publish the high-water mark (release): the worker's acquire-load
        // of active() must not order before the slot it will drain exists.
        std::uint32_t current = registration_.active.load(std::memory_order_relaxed);
        while (claimed + 1U > current &&
               !registration_.active.compare_exchange_weak(
                   current, claimed + 1U, std::memory_order_release,
                   std::memory_order_relaxed)) {
            // bounded: current strictly increases per retry
        }
        for (Entry& entry : tls) {
            if (entry.owner == nullptr) {
                entry.owner = this;
                entry.slot = static_cast<std::int32_t>(claimed);
                break;
            }
        }
        return static_cast<std::int8_t>(claimed);
    }

    /// INLINE end-of-span path (design D8): end stamp, one SPSC try_push
    /// (full ring → owner-side buffer_full bump, never blocking), TLS stack
    /// truncation. No lock, no allocation, no indirect call.
    bool finish_span(SpanRecord& record, std::int8_t slot,
                     std::uint8_t stack_index) noexcept {
        if (slot >= 0) {
            const std::size_t index = static_cast<std::size_t>(slot);
            record.end = stamps_[index].sample();
            if (!shards_[index].try_push(record)) {
                Shard_state& state = state_[index];
                state.buffer_full.store(
                    state.buffer_full.load(std::memory_order_relaxed) + 1U,
                    std::memory_order_relaxed);
            }
        }
        detail::active_span_stack().truncate_to(stack_index);
        return true;
    }

    /// Rate-limit policy reads for the start pipeline (plain fields written
    /// once at construction — happens-before via thread creation).
    [[nodiscard]] bool rate_policy_enabled() const noexcept { return policy_.enabled; }
    [[nodiscard]] std::uint64_t rate_max_spans() const noexcept {
        return policy_.max_spans_per_window;
    }
    [[nodiscard]] std::uint64_t rate_window_ns() const noexcept { return policy_.window_ns; }

    /// Consumer-only round-robin cursor (the worker is the sole pop() caller).
    std::uint32_t drain_shard_{};
    std::uint32_t drain_shard_pops_{};

    // -- cold-path machinery (out-of-line; provider.cpp instantiates) ------

    /// Consumer end: round-robin across the registered shards with a
    /// 16-record per-shard per-sweep slice (one flooded shard cannot starve
    /// the others); BufferEmpty when every registered shard is momentarily
    /// empty. Consumer thread only.
    [[nodiscard]] core::Status pop(Record& out) noexcept;

    /// The background consumer body: self-pin (failures into start_), sink
    /// start, drain loop with idle backoff (64 failed sweeps → 200 µs
    /// sleeps), flush handshake, shutdown drain-on-join + sink stop.
    void consume_loop(std::stop_token stop) noexcept;

    /// Flush publication: synchronize the sink, record the last sync error,
    /// publish the drained sequence (release).
    void publish_sync(std::uint64_t sequence) noexcept;

    // -- state (design D5 layout) ------------------------------------------

    /// Shard pool: slot s's producer end is touched ONLY by its owner
    /// thread; the single consumer drains all.
    alignas(64) std::array<core::SpscRingBuffer<SpanRecord, ShardCapacity>, Max_threads>
        shards_{};

    /// One stamp source PER SLOT: single-writer per slot keeps sampling
    /// race-free with zero atomics (design D6/D8).
    std::array<chrono::Stamp_source, Max_threads> stamps_{};

    /// Per-shard accounting + throttle window: the two counters are written
    /// ONLY by the slot's owner (relaxed store of prev+1 — never a contended
    /// RMW); the window fields are plain single-writer state. One cache line
    /// per shard.
    struct alignas(64) Shard_state {
        std::atomic<std::uint64_t> rate_limited{}; ///< owner stores, others load
        std::atomic<std::uint64_t> buffer_full{};  ///< owner stores, others load
        std::uint64_t window_start_ns{};           ///< owner-only plain state
        std::uint64_t tokens_used{};               ///< owner-only plain state
        std::uint8_t pad_[32]{};
    };
    static_assert(sizeof(Shard_state) % 64 == 0, "shard state owns whole lines");
    Shard_state state_[Max_threads]{};

    /// Slot registration (D13): next_slot relaxed fetch_add; active released
    /// max-store so the consumer observes slots it may drain.
    struct alignas(64) Registration_cell {
        std::atomic<std::uint32_t> next_slot{}; ///< relaxed fetch_add
        std::atomic<std::uint32_t> active{};    ///< release fetch_max (CAS loop)
        std::uint8_t pad_[56]{};
    } registration_{};

    /// Pool-exhaustion counter (relaxed; misconfiguration diagnostics).
    struct alignas(64) Exhausted_cell {
        std::atomic<std::uint64_t> slot_exhausted{};
        std::uint8_t pad_[56]{};
    } exhausted_{};

    /// Runtime control (D9): bit0 enabled, bits 1-2 sampling, rest reserved.
    /// ONE relaxed load per span start; relaxed stores on change.
    struct alignas(64) Control_cell {
        std::atomic<std::uint8_t> state{
            static_cast<std::uint8_t>(control_enabled_bit |
                                      (static_cast<std::uint8_t>(
                                           SamplingDecision::RecordAndSample)
                                       << control_sampling_shift))};
        std::uint8_t pad_[63]{};
    } control_{};

    /// Worker startup snapshot: one-shot release/acquire publication.
    struct alignas(64) Start_cell {
        std::atomic<bool> launched{};       ///< set (release) once the jthread exists
        std::atomic<bool> started{};        ///< set (release) after the start sequence
        std::atomic<std::uint8_t> code{};   ///< first start-phase ErrorCode snapshot
        std::atomic<std::uint64_t> mask{};  ///< post-pin affinity as the worker sees it
        std::uint8_t pad_[48]{};
    } start_{};

    /// Flush handshake (D10): requested fetch_add(release) by the flushing
    /// thread; drained store(release) by the worker after sink flush;
    /// sync_error explains the last publication.
    struct alignas(64) Flush_cell {
        std::atomic<std::uint64_t> requested{};
        std::atomic<std::uint64_t> drained{};
        std::atomic<std::uint8_t> sync_error{}; ///< ErrorCode+1, 0 == clean
        std::uint8_t pad_[47]{};
    } flush_{};

    /// Rate-limit policy: plain fields written once at construction (before
    /// any producer or worker exists), read on the start path.
    struct {
        std::uint64_t max_spans_per_window{0};
        std::uint64_t window_ns{0};
        bool enabled{false};
    } policy_{};

    TracerProvider_config config_{};
    detail::Sink_binding binding_{};

    /// Tracer pool: compile-time capacity; the EFFECTIVE limit is
    /// min(config.max_tracers, tracer_pool_capacity) — the configuration
    /// sizes usage, the compile-time constant sizes storage.
    static constexpr std::size_t tracer_pool_capacity = 64;
    struct TracerSlot {
        char name[SpanRecord::name_capacity + 1]{}; ///< inline scope-name storage
        bool occupied{false};
        // The Tracer object is placement-constructed into tail padding on
        // claim (see get_tracer): provider pointer + name pointer, 16 bytes,
        // 8-aligned — fits the slot's alignment without a separate arena.
    };
    alignas(64) std::array<std::byte, tracer_pool_capacity *
                                             (sizeof(TracerSlot) + sizeof(Tracer<ShardCapacity, Max_threads>))>
        tracer_storage_{};
    std::size_t tracer_limit_{0};

    /// Registry lock (control plane only — D12).
    mutable std::mutex tracer_mutex_{};

    /// Liveness flag: release-store in the destructor, acquire-load at every
    /// span start — handle-after-death detection (NotInitialized).
    std::atomic<bool> alive_{true};

    /// The background consumer — LAST member: std::jthread requests stop +
    /// joins before the pool, cells, sink binding, and registry die, and the
    /// final drain in consume_loop still sees all of them alive.
    std::jthread worker_{};

    // Layout self-verification (design D5): the cells own whole cache lines.
    static_assert(sizeof(Registration_cell) == 64);
    static_assert(sizeof(Exhausted_cell) == 64);
    static_assert(sizeof(Control_cell) == 64);
    static_assert(sizeof(Start_cell) == 64);
    static_assert(sizeof(Flush_cell) == 64);
};

// --- out-of-line cold members (definitions; provider.cpp explicitly
// instantiates the default parameters). --------------------------------------

template <std::size_t ShardCapacity, std::size_t Max_threads>
TracerProvider<ShardCapacity, Max_threads>::TracerProvider(
    const TracerProvider_config& config, detail::Sink_binding binding) noexcept
    : config_(config), binding_(binding) {
    policy_.enabled = config.max_spans_per_window != 0ULL && config.window_ns != 0ULL;
    policy_.max_spans_per_window = config.max_spans_per_window;
    policy_.window_ns = config.window_ns;
    tracer_limit_ = config.max_tracers < tracer_pool_capacity
                        ? static_cast<std::size_t>(config.max_tracers)
                        : tracer_pool_capacity;
}

template <std::size_t ShardCapacity, std::size_t Max_threads>
core::Status TracerProvider<ShardCapacity, Max_threads>::initialize() noexcept {
    if (start_.launched.load(std::memory_order_acquire)) {
        return {}; // idempotent: already initialized
    }
    for (auto& stamp : stamps_) {
        const core::Status calibrated = stamp.calibrate();
        if (!calibrated) {
            return calibrated;
        }
    }
    worker_ = std::jthread([this](std::stop_token stop) { consume_loop(stop); });
    start_.launched.store(true, std::memory_order_release);
    return {};
}

template <std::size_t ShardCapacity, std::size_t Max_threads>
TracerProvider<ShardCapacity, Max_threads>::~TracerProvider() {
    if (worker_.joinable()) {
        // consume_loop's shutdown path drains every accepted record, then
        // publishes + stops the sink, all while this object is still whole
        // (the jthread member is destroyed AFTER this body runs, and it is
        // declared last so it joins before the pool dies anyway).
        worker_.request_stop();
        worker_.join();
    } else if (binding_.on_span != nullptr) {
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

template <std::size_t ShardCapacity, std::size_t Max_threads>
core::Result<Tracer<ShardCapacity, Max_threads>*>
TracerProvider<ShardCapacity, Max_threads>::get_tracer(std::string_view scope_name) {
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
        (sizeof(TracerSlot) + alignof(Tracer<ShardCapacity, Max_threads>) - 1U) /
            alignof(Tracer<ShardCapacity, Max_threads>) *
            alignof(Tracer<ShardCapacity, Max_threads>) +
        sizeof(Tracer<ShardCapacity, Max_threads>);
    for (std::size_t i = 0; i < tracer_limit_; ++i) {
        auto* const slot = reinterpret_cast<TracerSlot*>(tracer_storage_.data() +
                                                          i * slot_stride);
        if (slot->occupied && std::string_view(slot->name) == scope_name) {
            return reinterpret_cast<Tracer<ShardCapacity, Max_threads>*>(
                reinterpret_cast<std::byte*>(slot) + sizeof(TracerSlot));
        }
    }
    for (std::size_t i = 0; i < tracer_limit_; ++i) {
        auto* const slot = reinterpret_cast<TracerSlot*>(tracer_storage_.data() +
                                                          i * slot_stride);
        if (!slot->occupied) {
            for (std::size_t c = 0; c < scope_name.size(); ++c) {
                slot->name[c] = scope_name[c];
            }
            slot->name[scope_name.size()] = '\0';
            slot->occupied = true;
            auto* const tracer = new (reinterpret_cast<std::byte*>(slot) + sizeof(TracerSlot))
                Tracer<ShardCapacity, Max_threads>(this, slot->name);
            return tracer;
        }
    }
    return std::unexpected(core::ErrorCode::BufferFull);
}

template <std::size_t ShardCapacity, std::size_t Max_threads>
core::Status TracerProvider<ShardCapacity, Max_threads>::pop(Record& out) noexcept {
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

template <std::size_t ShardCapacity, std::size_t Max_threads>
void TracerProvider<ShardCapacity, Max_threads>::consume_loop(
    std::stop_token stop) noexcept {
    // Start sequence ON the worker (design D10): self-pin first so no
    // scheduling window can run the body off-core; snapshot the resulting
    // mask for third-thread verification; then the sink's cold start. First
    // error wins the code snapshot; a failure never stops the pipeline.
    std::uint8_t first_error = 0U;
    if (config_.core_mask != 0ULL) {
        const core::Status pinned = detail::pin_self(config_.core_mask);
        if (!pinned) {
            first_error = static_cast<std::uint8_t>(pinned.error());
        }
    }
    std::uint64_t mask_now = 0ULL;
    static_cast<void>(detail::self_affinity(mask_now));
    start_.mask.store(mask_now, std::memory_order_relaxed);

    if (binding_.start != nullptr) {
        const core::Status started = binding_.start(binding_.self);
        if (!started && first_error == 0U) {
            first_error = static_cast<std::uint8_t>(started.error());
        }
    }
    start_.code.store(first_error, std::memory_order_relaxed);
    start_.started.store(true, std::memory_order_release);

    constexpr std::uint64_t idle_spin_budget = 64; // failed sweeps before sleeping

    Record record{};
    std::uint64_t idle = 0;
    while (!stop.stop_requested()) {
        std::size_t delivered = 0;
        while (delivered < 64 && pop(record)) { // bounded batch
            if (binding_.on_span != nullptr) {
                // Delivery failures never stop the drain: the consumer keeps
                // serving every shard; a failing sink surfaces its own state
                // through flush() (sync_error) and its diagnostics.
                static_cast<void>(binding_.on_span(binding_.self, record));
            }
            ++delivered;
        }
        if (delivered != 0) {
            idle = 0;
            continue;
        }
        // Momentarily empty: answer any flush request before going idle.
        const std::uint64_t request = flush_.requested.load(std::memory_order_acquire);
        if (request > flush_.drained.load(std::memory_order_relaxed)) {
            publish_sync(request);
        }
        if (++idle < idle_spin_budget) {
            continue;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    // Shutdown: drain what is still queued, synchronize once, publish, then
    // stop the sink (spec "Destruction does not lose accepted records").
    while (pop(record)) {
        if (binding_.on_span != nullptr) {
            static_cast<void>(binding_.on_span(binding_.self, record));
        }
    }
    publish_sync(flush_.requested.load(std::memory_order_relaxed));
    if (binding_.stop != nullptr) {
        binding_.stop(binding_.self);
    }
}

template <std::size_t ShardCapacity, std::size_t Max_threads>
void TracerProvider<ShardCapacity, Max_threads>::publish_sync(
    std::uint64_t sequence) noexcept {
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

template <std::size_t ShardCapacity, std::size_t Max_threads>
core::Status TracerProvider<ShardCapacity, Max_threads>::flush() noexcept {
    if (!start_.launched.load(std::memory_order_acquire)) {
        return std::unexpected(core::ErrorCode::NotInitialized); // not initialized
    }
    const std::uint64_t sequence =
        flush_.requested.fetch_add(1U, std::memory_order_release) + 1U;
    for (std::uint64_t spins = 0;
         flush_.drained.load(std::memory_order_acquire) < sequence; ++spins) {
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

template <std::size_t ShardCapacity, std::size_t Max_threads>
std::string TracerProvider<ShardCapacity, Max_threads>::to_string() const {
    std::string out = "TracerProvider(shard_capacity=";
    out += std::to_string(ShardCapacity);
    out += ", max_threads=" + std::to_string(Max_threads);
    out += ", core_mask=0x" + std::to_string(config_.core_mask);
    out += ", tracer_limit=" + std::to_string(tracer_limit_);
    out += ", enabled=" + std::string(is_enabled() ? "true" : "false");
    out += ", sampling=" + std::string(tracing::to_string(sampling()));
    out += ", worker_started=" + std::string(worker_started() ? "true" : "false");
    out += ", dropped=" + dropped().to_string();
    out += ")";
    return out;
}

} // namespace nexigen::tracing

/// The configuration and the drop snapshot format as their diagnostic
/// strings (mandate 11).
template <>
struct std::formatter<nexigen::tracing::TracerProvider_config>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::TracerProvider_config& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
template <>
struct std::formatter<nexigen::tracing::Dropped_stats> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::Dropped_stats& stats, Ctx& ctx) const {
        const std::string text = stats.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
template <std::size_t SC, std::size_t MT>
struct std::formatter<nexigen::tracing::TracerProvider<SC, MT>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::TracerProvider<SC, MT>& provider, Ctx& ctx) const {
        const std::string text = provider.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
