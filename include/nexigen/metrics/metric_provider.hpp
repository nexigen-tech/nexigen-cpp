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
// that delivers a batch every collect_interval. The harvester pins itself to
// the configured core mask before the first batch, so collection and sink I/O
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
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/detail/layout.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics {

/// Provider configuration: collection period, CPU affinity and registry
/// capacities. A zero collect_interval disables the background thread.
struct MetricProvider_config {
    std::chrono::nanoseconds collect_interval{0};
    std::uint64_t core_mask{0};
    std::uint32_t max_meters{16};
    std::uint32_t max_counters{64};
    std::uint32_t max_gauges{64};
    std::uint32_t max_statistics{32};
    std::uint32_t max_explicit_histograms{16};
    std::uint32_t max_exponential_histograms{8};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const MetricProvider_config& config);

/// Provider config JSON (cold path). Declared in this namespace so the
/// overloads are findable by ADL; definitions live in
/// src/metrics/snapshot_format.cpp (design D2).
void to_json(nlohmann::json& j, const MetricProvider_config& config);
void from_json(const nlohmann::json& j, MetricProvider_config& out) noexcept;

namespace detail {

/// Type-erased sink binding: plain function pointers, no virtual dispatch.
struct Sink_binding {
    void* self{nullptr};
    core::Status (*on_batch)(void*, const MetricBatchSnapshot&) noexcept {nullptr};
    core::Status (*flush)(void*) noexcept {nullptr};
};

} // namespace detail

/// The registry root and the only source of meters.
class MetricProvider {
public:
    /// Bind a sink and start the provider. The sink MUST outlive the
    /// provider.
    template <concepts::MetricSink Sink>
    MetricProvider(const MetricProvider_config& config, Sink& sink)
        : MetricProvider(config,
                         detail::Sink_binding{
                             static_cast<void*>(&sink),
                             [](void* self, const MetricBatchSnapshot& batch) noexcept -> core::Status {
                                 return static_cast<Sink*>(self)->on_batch(batch);
                             },
                             [](void* self) noexcept -> core::Status {
                                 return static_cast<Sink*>(self)->flush();
                             }}) {}

    MetricProvider(const MetricProvider&) = delete;
    MetricProvider& operator=(const MetricProvider&) = delete;
    MetricProvider(MetricProvider&&) = delete;
    MetricProvider& operator=(MetricProvider&&) = delete;

    ~MetricProvider();

    /// Get (or create) the meter for a name/version pair. Repeating the same
    /// pair returns the same handle; a different version for an existing name
    /// is rejected.
    [[nodiscard]] core::Result<Meter*> get_meter(std::string_view name, std::string_view version);

    /// Collect and deliver one batch synchronously on the calling thread.
    [[nodiscard]] core::Status collect_now() noexcept;

    /// Flush the bound sink.
    [[nodiscard]] core::Status flush() noexcept;

    /// Result of the provider's startup steps (affinity pinning, ...).
    [[nodiscard]] core::Status startup_status() const noexcept;

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;

private:
    friend class Meter;

    MetricProvider(const MetricProvider_config& config, detail::Sink_binding binding);

    /// One registry slot: identity, classification and the harvest hooks.
    struct Slot_meta {
        MetricIdentity identity{};
        MetricKind kind{MetricKind::Counter};
        ConcurrencyPolicy concurrency{ConcurrencyPolicy::SingleThreaded};
        AccessPolicy access{AccessPolicy::Readable};
        ResetPolicy reset{ResetPolicy::Cumulative};
        void* object{nullptr};
        void (*harvest)(void*, MetricRecord&) noexcept{nullptr};
        void (*destroy)(void*) noexcept{nullptr};
        bool occupied{false};
    };

    /// One instrument kind: its slot table and its object arena.
    struct Kind_pool {
        Slot_meta* meta{nullptr};
        std::byte* arena{nullptr};
        std::size_t arena_size{0};
        std::size_t used{0};
        std::uint32_t slot_max{0};
    };

    static constexpr std::size_t kind_count = 5;

    /// Reserve a registry slot for a new instrument.
    [[nodiscard]] core::Status reserve_slot(MetricKind kind, std::size_t object_size,
                                            const MetricIdentity& identity,
                                            ConcurrencyPolicy concurrency, AccessPolicy access,
                                            ResetPolicy reset,
                                            void (*harvest)(void*, MetricRecord&) noexcept,
                                            void (*destroy)(void*) noexcept, void*& object_out,
                                            Slot_meta*& meta_out);

    /// True when the meter already owns an instrument with that name.
    [[nodiscard]] bool has_duplicate(const MetricIdentity& meter_identity,
                                     std::string_view name) const noexcept;

    /// Background harvester body.
    void harvest_loop(std::stop_token stop) noexcept;

    static std::size_t kind_index(MetricKind kind) noexcept;
    static std::size_t kind_object_size(MetricKind kind) noexcept;

    MetricProvider_config config_{};
    detail::Sink_binding binding_{};

    std::byte* block_{nullptr};
    std::size_t block_size_{0};
    Kind_pool pools_[kind_count]{};

    Meter** meters_{nullptr};
    std::size_t meter_capacity_{0};
    std::byte* meter_arena_{nullptr};
    std::size_t meter_arena_size_{0};
    std::size_t meter_used_{0};

    MetricRecord* records_{nullptr};
    std::uint32_t record_capacity_{0};

    std::jthread harvester_{};
    std::mutex harvest_mutex_{};
    std::mutex wake_mutex_{};
    std::condition_variable_any wake_cv_{};
    std::atomic<bool> alive_{true};
    std::atomic<core::ErrorCode> startup_error_{core::ErrorCode::Ok};
    std::int64_t utc_offset_ns_{0};
};

} // namespace nexigen::metrics

// --- Meter creation methods (defined here: they need the complete provider).
namespace nexigen::metrics {

template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
core::Result<Counter<Concurrency, Access, Reset>*>
Meter::create_counter(std::string_view name) {
    // Publish the slot under the harvest lock (design D11): construction and
    // occupancy happen-before the background harvester can observe them.
    std::lock_guard<std::mutex> lock(provider_->harvest_mutex_);
    MetricIdentity identity{};
    detail::copy_meter_identity(identity, identity_);
    if (!detail::copy_name(identity.metric_name, name)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    void* object = nullptr;
    MetricProvider::Slot_meta* meta = nullptr;
    using Instrument = Counter<Concurrency, Access, Reset>;
    const core::Status reserved =
        provider_->reserve_slot(MetricKind::Counter, sizeof(Instrument), identity, Concurrency,
                                Access, Reset, &Instrument::harvest_thunk,
                                &Instrument::destroy_thunk, object, meta);
    if (!reserved) {
        return std::unexpected(reserved.error());
    }
    auto* instrument = new (object) Instrument(MeterPasskey{});
    instrument->identity_ = &meta->identity;
    return instrument;
}

template <typename ValueType, ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
core::Result<Gauge<ValueType, Concurrency, Access, Reset>*>
Meter::create_gauge(std::string_view name) {
    // Publish under the harvest lock (design D11).
    std::lock_guard<std::mutex> lock(provider_->harvest_mutex_);
    MetricIdentity identity{};
    detail::copy_meter_identity(identity, identity_);
    if (!detail::copy_name(identity.metric_name, name)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    void* object = nullptr;
    MetricProvider::Slot_meta* meta = nullptr;
    using Instrument = Gauge<ValueType, Concurrency, Access, Reset>;
    const core::Status reserved =
        provider_->reserve_slot(MetricKind::Gauge, sizeof(Instrument), identity, Concurrency, Access,
                                Reset, &Instrument::harvest_thunk, &Instrument::destroy_thunk, object,
                                meta);
    if (!reserved) {
        return std::unexpected(reserved.error());
    }
    auto* instrument = new (object) Instrument(MeterPasskey{});
    instrument->identity_ = &meta->identity;
    return instrument;
}

template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
core::Result<DescriptiveStatistics<Concurrency, Access, Reset>*>
Meter::create_statistics(std::string_view name) {
    // Publish under the harvest lock (design D11).
    std::lock_guard<std::mutex> lock(provider_->harvest_mutex_);
    MetricIdentity identity{};
    detail::copy_meter_identity(identity, identity_);
    if (!detail::copy_name(identity.metric_name, name)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    void* object = nullptr;
    MetricProvider::Slot_meta* meta = nullptr;
    using Instrument = DescriptiveStatistics<Concurrency, Access, Reset>;
    const core::Status reserved =
        provider_->reserve_slot(MetricKind::DescriptiveStatistics, sizeof(Instrument), identity,
                                Concurrency, Access, Reset, &Instrument::harvest_thunk,
                                &Instrument::destroy_thunk, object, meta);
    if (!reserved) {
        return std::unexpected(reserved.error());
    }
    auto* instrument = new (object) Instrument(MeterPasskey{});
    instrument->identity_ = &meta->identity;
    return instrument;
}

template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
core::Result<ExplicitBucketHistogram<Concurrency, Access, Reset>*>
Meter::create_explicit_histogram(std::string_view name, std::span<const double> bounds) {
    // Publish under the harvest lock (design D11).
    std::lock_guard<std::mutex> lock(provider_->harvest_mutex_);
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
    MetricIdentity identity{};
    detail::copy_meter_identity(identity, identity_);
    if (!detail::copy_name(identity.metric_name, name)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    void* object = nullptr;
    MetricProvider::Slot_meta* meta = nullptr;
    using Instrument = ExplicitBucketHistogram<Concurrency, Access, Reset>;
    const core::Status reserved =
        provider_->reserve_slot(MetricKind::ExplicitHistogram, sizeof(Instrument), identity,
                                Concurrency, Access, Reset, &Instrument::harvest_thunk,
                                &Instrument::destroy_thunk, object, meta);
    if (!reserved) {
        return std::unexpected(reserved.error());
    }
    auto* instrument = new (object) Instrument(MeterPasskey{});
    instrument->install_bounds(bounds.data(), static_cast<std::uint32_t>(bounds.size()));
    instrument->identity_ = &meta->identity;
    return instrument;
}

template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
core::Result<ExponentialHistogram<Concurrency, Access, Reset>*>
Meter::create_exponential_histogram(std::string_view name, std::int32_t scale) {
    // Publish under the harvest lock (design D11).
    std::lock_guard<std::mutex> lock(provider_->harvest_mutex_);
    if (scale < 0 || scale > detail::max_exponential_scale) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    MetricIdentity identity{};
    detail::copy_meter_identity(identity, identity_);
    if (!detail::copy_name(identity.metric_name, name)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    void* object = nullptr;
    MetricProvider::Slot_meta* meta = nullptr;
    using Instrument = ExponentialHistogram<Concurrency, Access, Reset>;
    const core::Status reserved =
        provider_->reserve_slot(MetricKind::ExponentialHistogram, sizeof(Instrument), identity,
                                Concurrency, Access, Reset, &Instrument::harvest_thunk,
                                &Instrument::destroy_thunk, object, meta);
    if (!reserved) {
        return std::unexpected(reserved.error());
    }
    auto* instrument = new (object) Instrument(MeterPasskey{}, scale);
    instrument->identity_ = &meta->identity;
    return instrument;
}

} // namespace nexigen::metrics

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::MetricProvider_config> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::MetricProvider_config& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};


