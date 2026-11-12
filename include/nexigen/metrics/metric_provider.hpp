// nexigen::metrics — the metric provider: registry, factory root and
// background harvester (design D6/D14; spec metrics/metric-provider).
//
// MetricProvider owns every instrument. It pre-allocates ONE aligned block at
// construction (arenas, slot tables and the harvest buffer), so creating
// meters and instruments, recording, harvesting and shutting down never touch
// the heap. Registry traversal walks contiguous slot tables.
//
// The provider binds a MetricSink (type-erased through plain function
// pointers — no virtual dispatch, no RTTI) and runs a std::jthread harvester
// that delivers a batch every collect_interval. The harvester applies its
// copied worker ThreadSpec before the first batch, so collection and sink I/O
// never preempt the real-time threads.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/worker_thread.hpp>
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/detail/instrument_helpers.hpp>
#include <nexigen/metrics/detail/layout.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/metric_provider_config.hpp>
#include <nexigen/metrics/sink_binding.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics {

/// @brief The registry root, factory root and background harvester.
///
/// A provider owns every meter and instrument. Construction pre-allocates one
/// aligned block holding the per-kind object arenas, slot tables, meter storage
/// and the harvest record buffer, so meter/instrument creation, recording and
/// harvesting never touch the heap and registry traversal stays contiguous.
///
/// A bound `MetricSink` is type-erased through plain function pointers (no
/// virtual dispatch, no RTTI). Unless the collection interval is zero, a
/// background `std::jthread` applies its copied worker `ThreadSpec` and then
/// delivers a `MetricBatchSnapshot` every `collect_interval`; the affinity step
/// happens before the first batch so collection and sink I/O never preempt the
/// real-time threads.
///
/// Creation and control-plane calls are cold path; instrument recording is the
/// only hot path and never takes the provider's mutexes.
///
/// @note Spec `metrics/metric-provider`, requirement "Pre-allocated aligned
///       metric registry".
/// @note Spec `metrics/metric-provider`, requirement "Background harvesting and
///       collection period".
/// @warning Instruments and meters are provider-owned handles and MUST NOT
///          outlive the provider.
class MetricProvider {
public:
    /// @brief Bind a sink and start the provider.
    ///
    /// Wraps the typed sink in a `SinkBinding` of plain function pointers and
    /// delegates to the private constructor, which allocates the registry and
    /// starts the harvester when the configured interval is non-zero.
    ///
    /// @tparam Sink Concrete sink type satisfying the `MetricSink` concept; its
    ///         `on_batch` and `flush` must be callable without throwing.
    /// @param config Provider configuration (capacities, interval, worker spec).
    /// @param sink Sink receiving batches; it MUST outlive the provider.
    /// @note Spec `metrics/metric-provider`, requirement "Background harvesting
    ///       and collection period".
    /// @warning The sink reference is stored type-erased: destroying it before
    ///          the provider leaves a dangling callback target.
    template <concepts::MetricSink Sink>
    MetricProvider(const MetricProviderConfig& config, Sink& sink)
        : MetricProvider(config, detail::SinkBinding{
                                     static_cast<void*>(&sink),
                                     [](void* self,
                                        const MetricBatchSnapshot& batch) noexcept -> core::Status {
                                         return static_cast<Sink*>(self)->on_batch(batch);
                                     },
                                     [](void* self) noexcept -> core::Status {
                                         return static_cast<Sink*>(self)->flush();
                                     }}) {}

    /// @brief Deleted: the provider is the unique registry owner.
    MetricProvider(const MetricProvider&) = delete;
    /// @brief Deleted: the provider is the unique registry owner.
    MetricProvider& operator=(const MetricProvider&) = delete;
    /// @brief Deleted: registry storage is referenced by raw handles.
    MetricProvider(MetricProvider&&) = delete;
    /// @brief Deleted: registry storage is referenced by raw handles.
    MetricProvider& operator=(MetricProvider&&) = delete;

    /// @brief Stop the harvester, drain one final batch, flush and free storage.
    ///
    /// Stops and joins the background thread, delivers one final collection of
    /// pending values, flushes the bound sink, then releases the pre-allocated
    /// block without allocating or throwing.
    ///
    /// @note Spec `metrics/metric-provider`, requirement "Provider shutdown".
    ~MetricProvider();

    /// @brief Get or create the meter for a name/version pair.
    ///
    /// Repeating the same pair returns the same handle. A name already bound to
    /// a different version is rejected. Lookup, creation and publication happen
    /// under the harvest mutex, so this call is linearized with harvesting.
    ///
    /// @param name Meter name; must be non-empty and fit the name buffer.
    /// @param version Meter version; must be non-empty and fit the name buffer.
    /// @return Pointer to the provider-owned meter on success. The error arm is
    ///         `core::ErrorCode::InvalidArgument` for invalid or conflicting
    ///         names and `core::ErrorCode::BufferFull` when meter capacity is
    ///         exhausted.
    /// @note Spec `metrics/metric-provider`, requirement "Factory hierarchy and
    ///       passkey construction".
    /// @note Spec `metrics/metric-provider`, requirement "Meter publication is
    ///       serialized with harvesting".
    [[nodiscard]] core::Result<Meter*> get_meter(std::string_view name, std::string_view version);

    /// @brief Collect and deliver one batch synchronously on the calling thread.
    ///
    /// Snapshots every registered instrument into the harvest buffer and hands
    /// exactly one batch to the sink before returning. Works whether or not the
    /// background harvester is enabled.
    ///
    /// @return Success once the batch was delivered; otherwise the sink or
    ///         internal harvest failure. The provider stays usable afterwards.
    /// @note Spec `metrics/metric-provider`, requirement "Background harvesting
    ///       and collection period".
    /// @note Spec `metrics/metric-provider`, requirement "Provider control
    ///       plane".
    [[nodiscard]] core::Status collect_now() noexcept;

    /// @brief Flush the bound sink.
    ///
    /// @return Success, or the sink's I/O failure propagated unchanged.
    /// @note Spec `metrics/metric-provider`, requirement "Provider control
    ///       plane".
    [[nodiscard]] core::Status flush() noexcept;

    /// @brief Result of the provider's startup steps (affinity pinning, ...).
    ///
    /// @return Success when startup attributes were applied or best-effort
    ///         failures were tolerated; otherwise the `Required` failure that
    ///         prevented the harvest loop.
    /// @note Spec `metrics/metric-provider`, requirement "CPU core affinity".
    [[nodiscard]] core::Status startup_status() const noexcept;

    /// @brief Immutable worker-attribute report, valid after startup publication.
    ///
    /// @return The harvest worker's applied name, affinity, scheduling policy and
    ///         per-attribute statuses.
    /// @note Spec `metrics/metric-provider`, requirement "CPU core affinity".
    [[nodiscard]] const core::ThreadApplyReport& worker_thread_report() const noexcept {
        return worker_.report();
    }

    /// @brief Cold-path diagnostic string describing the provider.
    ///
    /// @return Implementation-defined text with capacities and harvester state;
    ///         follows project mandate 9.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Single source of meters: only the provider may construct them.
    friend class Meter;

    /// @brief Owning constructor: allocate the registry and start the harvester.
    ///
    /// @param config Provider configuration copied into the instance.
    /// @param binding Type-erased sink callbacks; the sink itself must outlive
    ///        the provider.
    MetricProvider(const MetricProviderConfig& config, detail::SinkBinding binding);

    /// @brief One registry slot: identity, classification and harvest hooks.
    ///
    /// `object` points into the kind's arena; `harvest` and `destroy` are the
    /// type-erased operations the provider needs without RTTI. `occupied` marks
    /// a live slot.
    struct SlotMeta {
        /// @brief Identity (meter/metric name, version, unit).
        MetricIdentity identity{};
        /// @brief Instrument kind selecting arena and harvest semantics.
        MetricKind kind{MetricKind::Counter};
        /// @brief Concurrency policy reported in the harvested record.
        ConcurrencyPolicy concurrency{ConcurrencyPolicy::SingleThreaded};
        /// @brief Access policy reported in the harvested record.
        AccessPolicy access{AccessPolicy::Readable};
        /// @brief Reset policy reported in the harvested record.
        ResetPolicy reset{ResetPolicy::Cumulative};
        /// @brief Instrument object inside the kind's arena.
        void* object{nullptr};
        /// @brief Type-erased harvest hook writing a `MetricRecord`.
        void (*harvest)(void*, MetricRecord&) noexcept {nullptr};
        /// @brief Type-erased destructor run at provider shutdown.
        void (*destroy)(void*) noexcept {nullptr};
        /// @brief True while this slot names a live instrument.
        bool occupied{false};
    };

    /// @brief One instrument kind: its slot table and its object arena.
    ///
    /// `meta` and `arena` point into the provider's single aligned block;
    /// `used` carves objects out of `arena` up to `arena_size`, and `slot_max`
    /// bounds the slot table.
    struct KindPool {
        /// @brief Slot table, one entry per possible instrument of this kind.
        SlotMeta* meta{nullptr};
        /// @brief Aligned object arena for this kind.
        std::byte* arena{nullptr};
        /// @brief Size of `arena` in bytes.
        std::size_t arena_size{0};
        /// @brief Bytes already carved out of `arena`.
        std::size_t used{0};
        /// @brief Number of slots in `meta`.
        std::uint32_t slot_max{0};
    };

    /// @brief Number of instrument kinds in `pools_`.
    static constexpr std::size_t kind_count = 6;

    /// @brief Reserve a registry slot for a new instrument.
    ///
    /// Under the harvest mutex, checks the kind's capacity, carves an aligned
    /// object out of the arena, records the metadata and marks the slot occupied.
    ///
    /// @param kind Instrument kind selecting the pool.
    /// @param object_size Size of the instrument object to reserve.
    /// @param identity Fully populated metric identity to publish.
    /// @param concurrency Concurrency policy stored in the slot.
    /// @param access Access policy stored in the slot.
    /// @param reset Reset policy stored in the slot.
    /// @param harvest Type-erased harvest hook for the instrument.
    /// @param destroy Type-erased destructor for the instrument.
    /// @param object_out Receives the reserved, still unconstructed object.
    /// @param meta_out Receives the slot metadata for identity wiring.
    /// @return Success, `InvalidArgument` on a duplicate/invalid identity, or
    ///         `BufferFull` when the kind's pool is exhausted.
    /// @note Spec `metrics/metric-provider`, requirement "Pre-allocated aligned
    ///       metric registry".
    [[nodiscard]] core::Status
    reserve_slot(MetricKind kind, std::size_t object_size, const MetricIdentity& identity,
                 ConcurrencyPolicy concurrency, AccessPolicy access, ResetPolicy reset,
                 void (*harvest)(void*, MetricRecord&) noexcept, void (*destroy)(void*) noexcept,
                 void*& object_out, SlotMeta*& meta_out);

    /// @brief True when the meter already owns an instrument with that name.
    ///
    /// @param meter_identity Identity of the owning meter.
    /// @param name Instrument name to look for.
    /// @return `true` if a slot with the same meter and metric name is occupied.
    /// @note Spec `metrics/metric-provider`, requirement "Pre-allocated aligned
    ///       metric registry".
    [[nodiscard]] bool has_duplicate(const MetricIdentity& meter_identity,
                                     std::string_view name) const noexcept;

    /// @brief Background harvester body.
    ///
    /// Applies the copied worker attributes, then waits for the collection
    /// period (or an explicit wake) and delivers one batch per wake-up until the
    /// stop token is signalled.
    ///
    /// @param stop Stop token set by the owning `WorkerThread` on shutdown.
    /// @note Spec `metrics/metric-provider`, requirement "Background harvesting
    ///       and collection period".
    void harvest_loop(std::stop_token stop) noexcept;

    /// @brief Map a `MetricKind` to its index in `pools_`.
    ///
    /// @param kind Instrument kind.
    /// @return Zero-based pool index.
    static std::size_t kind_index(MetricKind kind) noexcept;
    /// @brief Static object size of a kind's instrument, for arena carving.
    ///
    /// @param kind Instrument kind.
    /// @return Size in bytes of the concrete instrument type.
    static std::size_t kind_object_size(MetricKind kind) noexcept;

    /// @brief Configuration copied at construction.
    MetricProviderConfig config_{};
    /// @brief Type-erased sink callbacks bound at construction.
    detail::SinkBinding binding_{};

    /// @brief Single aligned allocation backing arenas, slots and records.
    std::byte* block_{nullptr};
    /// @brief Size of `block_` in bytes.
    std::size_t block_size_{0};
    /// @brief Per-kind slot tables and object arenas inside `block_`.
    KindPool pools_[kind_count]{};

    /// @brief Meter-handle table inside `block_`.
    Meter** meters_{nullptr};
    /// @brief Number of entries in `meters_`.
    std::size_t meter_capacity_{0};
    /// @brief Arena holding `Meter` objects inside `block_`.
    std::byte* meter_arena_{nullptr};
    /// @brief Size of `meter_arena_` in bytes.
    std::size_t meter_arena_size_{0};
    /// @brief Bytes already carved out of `meter_arena_`.
    std::size_t meter_used_{0};

    /// @brief Harvest record buffer reused for every batch.
    MetricRecord* records_{nullptr};
    /// @brief Capacity of `records_`.
    std::uint32_t record_capacity_{0};

    /// @brief Adapts the provider to the owning `WorkerThread`.
    struct Body {
        /// @brief Provider whose `harvest_loop` the worker runs.
        MetricProvider* self{nullptr};
        /// @brief Thread entry point forwarding to `harvest_loop`.
        ///
        /// @param stop Stop token owned by the worker thread.
        void run(std::stop_token stop) noexcept { self->harvest_loop(stop); }
    };

    /// @brief Serializes registry publication with harvesting and collection.
    std::mutex harvest_mutex_{};
    /// @brief Serializes condition-variable waits and explicit wakes.
    std::mutex wake_mutex_{};
    /// @brief Wakes the harvester early on shutdown or explicit collection.
    std::condition_variable_any wake_cv_{};
    /// @brief Provider liveness flag used by debug lifetime checks.
    std::atomic<bool> alive_{true};
    /// @brief First startup error, published through `startup_status()`.
    std::atomic<core::ErrorCode> startup_error_{core::ErrorCode::Ok};
    /// @brief Cached UTC offset in nanoseconds for collection timestamps.
    std::int64_t utc_offset_ns_{0};
    /// @brief Harvest worker; declared LAST so it joins before the registry dies.
    core::WorkerThread<Body> worker_{};
};

} // namespace nexigen::metrics

// --- Meter creation methods (defined here: they need the complete provider).
namespace nexigen::metrics {

/// @brief Shared factory prologue: reserve a slot, construct, wire identity.
///
/// Definition of the declaration in `meter.hpp`; see it for the full contract.
/// Holds the provider's harvest mutex across construction and publication so the
/// background harvester observes either no slot or a fully constructed
/// instrument.
///
/// @note Spec `metrics/metric-provider`, requirement "Meter instrument
///       creation".
template <class Instrument, class Construct>
core::Result<Instrument*>
Meter::create_instrument(MetricKind kind, std::string_view name, std::string_view unit,
                         ConcurrencyPolicy concurrency, AccessPolicy access, ResetPolicy reset,
                         Construct&& construct) {
    // Publish the slot under the harvest lock (design D11): construction and
    // occupancy happen-before the background harvester can observe them.
    std::lock_guard<std::mutex> lock(provider_->harvest_mutex_);
    MetricIdentity identity{};
    detail::copy_meter_identity(identity, identity_);
    if (!detail::copy_name(identity.metric_name, name)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!unit.empty() && !detail::copy_name(identity.unit, unit)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    void* object = nullptr;
    MetricProvider::SlotMeta* meta = nullptr;
    const core::Status reserved = provider_->reserve_slot(
        kind, sizeof(Instrument), identity, concurrency, access, reset, &Instrument::harvest_thunk,
        &detail::destroy_instrument<Instrument>, object, meta);
    if (!reserved) {
        return std::unexpected(reserved.error());
    }
    Instrument* instrument = construct(object);
    instrument->identity_ = &meta->identity;
    return instrument;
}

/// @brief Definition of `Meter::create_counter`; see `meter.hpp`.
///
/// @note Spec `metrics/metric-provider`, requirement "Meter instrument
///       creation".
template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
core::Result<Counter<Concurrency, Access, Reset>*> Meter::create_counter(std::string_view name,
                                                                         std::string_view unit) {
    using Instrument = Counter<Concurrency, Access, Reset>;
    return create_instrument<Instrument>(
        MetricKind::Counter, name, unit, Concurrency, Access, Reset,
        [](void* object) -> Instrument* { return new (object) Instrument(MeterPasskey{}); });
}

/// @brief Definition of `Meter::create_gauge`; see `meter.hpp`.
///
/// @note Spec `metrics/metric-provider`, requirement "Meter instrument
///       creation".
template <typename ValueType, ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
core::Result<Gauge<ValueType, Concurrency, Access, Reset>*>
Meter::create_gauge(std::string_view name, std::string_view unit) {
    using Instrument = Gauge<ValueType, Concurrency, Access, Reset>;
    return create_instrument<Instrument>(
        MetricKind::Gauge, name, unit, Concurrency, Access, Reset,
        [](void* object) -> Instrument* { return new (object) Instrument(MeterPasskey{}); });
}

/// @brief Definition of `Meter::create_state`; see `meter.hpp`.
///
/// @note Spec `metrics/metric-provider`, requirement "Meter instrument
///       creation".
template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
core::Result<StateGauge<Concurrency, Access, Reset>*> Meter::create_state(std::string_view name,
                                                                          std::string_view unit) {
    using Instrument = StateGauge<Concurrency, Access, Reset>;
    return create_instrument<Instrument>(
        MetricKind::State, name, unit, Concurrency, Access, Reset,
        [](void* object) -> Instrument* { return new (object) Instrument(MeterPasskey{}); });
}

/// @brief Definition of `Meter::create_statistics`; see `meter.hpp`.
///
/// @note Spec `metrics/metric-provider`, requirement "Meter instrument
///       creation".
template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
core::Result<DescriptiveStatistics<Concurrency, Access, Reset>*>
Meter::create_statistics(std::string_view name, std::string_view unit) {
    using Instrument = DescriptiveStatistics<Concurrency, Access, Reset>;
    return create_instrument<Instrument>(
        MetricKind::DescriptiveStatistics, name, unit, Concurrency, Access, Reset,
        [](void* object) -> Instrument* { return new (object) Instrument(MeterPasskey{}); });
}

/// @brief Definition of `Meter::create_explicit_histogram`; see `meter.hpp`.
///
/// Validates the bounds before reserving a registry slot, then installs a copy of
/// them on the new instrument.
///
/// @note Spec `metrics/explicit-histogram`, requirement "Explicit bucket
///       configuration".
/// @note Spec `metrics/metric-provider`, requirement "Meter instrument
///       creation".
template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
core::Result<ExplicitBucketHistogram<Concurrency, Access, Reset>*>
Meter::create_explicit_histogram(std::string_view name, std::span<const double> bounds,
                                 std::string_view unit) {
    // Validate the bounds before touching the registry (no lock needed — the
    // span is caller-owned and read-only).
    if (bounds.empty() || bounds.size() > detail::max_explicit_bounds) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    for (std::size_t i = 0; i < bounds.size(); ++i) {
        if (!std::isfinite(bounds[i])) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (i > 0 && !(bounds[i] > bounds[i - 1])) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
    }
    using Instrument = ExplicitBucketHistogram<Concurrency, Access, Reset>;
    return create_instrument<Instrument>(
        MetricKind::ExplicitHistogram, name, unit, Concurrency, Access, Reset,
        [bounds](void* object) -> Instrument* {
            auto* instrument = new (object) Instrument(MeterPasskey{});
            instrument->install_bounds(bounds.data(), static_cast<std::uint32_t>(bounds.size()));
            return instrument;
        });
}

/// @brief Definition of `Meter::create_exponential_histogram`; see `meter.hpp`.
///
/// Rejects an out-of-range scale before reserving a registry slot, then
/// constructs the instrument at that finest resolution.
///
/// @note Spec `metrics/exponential-histogram`, requirement "Exponential scale
///       configuration".
/// @note Spec `metrics/metric-provider`, requirement "Meter instrument
///       creation".
template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
core::Result<ExponentialHistogram<Concurrency, Access, Reset>*>
Meter::create_exponential_histogram(std::string_view name, std::int32_t scale,
                                    std::string_view unit) {
    if (scale < 0 || scale > detail::max_exponential_scale) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    using Instrument = ExponentialHistogram<Concurrency, Access, Reset>;
    return create_instrument<Instrument>(MetricKind::ExponentialHistogram, name, unit, Concurrency,
                                         Access, Reset, [scale](void* object) -> Instrument* {
                                             return new (object) Instrument(MeterPasskey{}, scale);
                                         });
}

} // namespace nexigen::metrics

// MetricProviderConfig formatter lives in metric_provider_config.hpp.
