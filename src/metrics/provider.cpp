// nexigen::metrics — provider implementation: registry allocation, meter
// lookup, harvesting and the background worker (design D6/D14). See
// metric_provider.hpp for the contract.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/metrics/detail/affinity.hpp>
#include <nexigen/metrics/detail/layout.hpp>
#include <nexigen/metrics/metric_provider.hpp>

namespace nexigen::metrics {
namespace {

/// Zero the header of a histogram payload (the bucket arrays are owned by the
/// instrument's own snapshot, so only the scalar header is reset here).
void reset_histogram_header(HistogramSnapshot& snapshot) noexcept {
    snapshot.count = 0;
    snapshot.sum = 0.0;
    snapshot.min = 0.0;
    snapshot.max = 0.0;
    snapshot.ignored_samples = 0;
    snapshot.zero_count = 0;
    snapshot.effective_scale = 0;
    snapshot.p50 = 0.0;
    snapshot.p90 = 0.0;
    snapshot.p99 = 0.0;
    snapshot.p999 = 0.0;
    snapshot.bucket_count = 0;
}

} // namespace

std::size_t MetricProvider::kind_index(MetricKind kind) noexcept {
    switch (kind) {
        case MetricKind::Counter:               return 0;
        case MetricKind::Gauge:                 return 1;
        case MetricKind::DescriptiveStatistics: return 2;
        case MetricKind::ExplicitHistogram:     return 3;
        case MetricKind::ExponentialHistogram:  return 4;
    }
    return 0;
}

std::size_t MetricProvider::kind_object_size(MetricKind kind) noexcept {
    switch (kind) {
        case MetricKind::Counter:               return detail::max_counter_size;
        case MetricKind::Gauge:                 return detail::max_gauge_size;
        case MetricKind::DescriptiveStatistics: return detail::max_statistics_size;
        case MetricKind::ExplicitHistogram:     return detail::max_explicit_histogram_size;
        case MetricKind::ExponentialHistogram:  return detail::max_exponential_histogram_size;
    }
    return 0;
}

MetricProvider::MetricProvider(const MetricProvider_config& config, detail::Sink_binding binding)
    : config_(config), binding_(binding) {
    const std::uint32_t capacities[kind_count] = {
        config_.max_counters, config_.max_gauges, config_.max_statistics,
        config_.max_explicit_histograms, config_.max_exponential_histograms};

    // One aligned block: slot tables, object arenas, meter table, meter arena
    // and the harvest buffer.
    std::size_t total = 0;
    for (std::size_t i = 0; i < kind_count; ++i) {
        total += detail::align_up(static_cast<std::size_t>(capacities[i]) * sizeof(Slot_meta));
        total += detail::align_up(static_cast<std::size_t>(capacities[i]) * kind_object_size(
                                     static_cast<MetricKind>(i)));
    }
    total += detail::align_up(static_cast<std::size_t>(config_.max_meters) * sizeof(Meter*));
    total += detail::align_up(static_cast<std::size_t>(config_.max_meters) * detail::max_meter_size);
    record_capacity_ = 0;
    for (const std::uint32_t capacity : capacities) {
        record_capacity_ += capacity;
    }
    total += detail::align_up(static_cast<std::size_t>(record_capacity_) * sizeof(MetricRecord));

    block_ = static_cast<std::byte*>(::operator new(total, std::align_val_t{detail::instrument_alignment}));
    block_size_ = total;

    std::size_t offset = 0;
    for (std::size_t i = 0; i < kind_count; ++i) {
        const std::size_t meta_bytes =
            detail::align_up(static_cast<std::size_t>(capacities[i]) * sizeof(Slot_meta));
        const std::size_t arena_bytes =
            detail::align_up(static_cast<std::size_t>(capacities[i]) * kind_object_size(
                                 static_cast<MetricKind>(i)));
        pools_[i].meta = reinterpret_cast<Slot_meta*>(block_ + offset);
        std::memset(static_cast<void*>(pools_[i].meta), 0, meta_bytes);
        offset += meta_bytes;
        pools_[i].arena = block_ + offset;
        pools_[i].arena_size = arena_bytes;
        pools_[i].used = 0;
        pools_[i].slot_max = capacities[i];
        offset += arena_bytes;
    }
    const std::size_t meter_table_bytes =
        detail::align_up(static_cast<std::size_t>(config_.max_meters) * sizeof(Meter*));
    meters_ = reinterpret_cast<Meter**>(block_ + offset);
    std::memset(meters_, 0, meter_table_bytes);
    meter_capacity_ = config_.max_meters;
    offset += meter_table_bytes;
    meter_arena_ = block_ + offset;
    meter_arena_size_ =
        detail::align_up(static_cast<std::size_t>(config_.max_meters) * detail::max_meter_size);
    meter_used_ = 0;
    offset += meter_arena_size_;
    records_ = reinterpret_cast<MetricRecord*>(block_ + offset);

    std::int64_t offset_ns = 0;
    if (chrono::sample_utc_offset(offset_ns)) {
        utc_offset_ns_ = offset_ns;
    }

    if (config_.collect_interval > std::chrono::nanoseconds{0}) {
        harvester_ = std::jthread([this](std::stop_token stop) { harvest_loop(stop); });
    }
}

MetricProvider::~MetricProvider() {
    // Stop the harvester first: no collection may run while the registry is
    // being torn down.
    if (harvester_.joinable()) {
        harvester_.request_stop();
        {
            std::lock_guard<std::mutex> lock(wake_mutex_);
            wake_cv_.notify_all();
        }
        harvester_.join();
    }

    // Drain the sink once more so the last window is not lost.
    if (binding_.on_batch != nullptr) {
        static_cast<void>(collect_now());
    }
    if (binding_.flush != nullptr) {
        static_cast<void>(binding_.flush(binding_.self));
    }

    alive_.store(false, std::memory_order_release);

    // Destroy the instruments, then release the single allocation.
    for (auto& pool : pools_) {
        for (std::uint32_t i = 0; i < pool.slot_max; ++i) {
            Slot_meta& meta = pool.meta[i];
            if (meta.occupied && meta.destroy != nullptr) {
                meta.destroy(meta.object);
                meta.object = nullptr;
                meta.occupied = false;
            }
        }
    }
    for (std::size_t i = 0; i < meter_capacity_; ++i) {
        if (meters_[i] != nullptr) {
            meters_[i]->~Meter();
            meters_[i] = nullptr;
        }
    }
    if (block_ != nullptr) {
        ::operator delete(block_, std::align_val_t{detail::instrument_alignment});
        block_ = nullptr;
    }
}

core::Result<Meter*> MetricProvider::get_meter(std::string_view name, std::string_view version) {
    if (!alive_.load(std::memory_order_acquire)) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    MetricIdentity identity{};
    if (!detail::copy_name(identity.meter_name, name)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!detail::copy_name(identity.meter_version, version)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    // Idempotent lookup: the same name+version returns the same handle; the
    // same name with a different version is a conflict.
    for (std::size_t i = 0; i < meter_capacity_; ++i) {
        Meter* meter = meters_[i];
        if (meter == nullptr) {
            continue;
        }
        if (detail::view_name(meter->identity().meter_name) != name) {
            continue;
        }
        if (detail::view_name(meter->identity().meter_version) != version) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        return meter;
    }

    std::size_t slot = meter_capacity_;
    for (std::size_t i = 0; i < meter_capacity_; ++i) {
        if (meters_[i] == nullptr) {
            slot = i;
            break;
        }
    }
    if (slot == meter_capacity_) {
        return std::unexpected(core::ErrorCode::BufferFull);
    }
    const std::size_t object_bytes = detail::align_up(sizeof(Meter));
    if (meter_used_ + object_bytes > meter_arena_size_) {
        return std::unexpected(core::ErrorCode::BufferFull);
    }
    void* address = meter_arena_ + meter_used_;
    meter_used_ += object_bytes;
    auto* meter = new (address) Meter();
    meter->provider_ = this;
    meter->identity_ = identity;
    meters_[slot] = meter;
    return meter;
}

core::Status MetricProvider::reserve_slot(MetricKind kind, std::size_t object_size,
                                           const MetricIdentity& identity,
                                           ConcurrencyPolicy concurrency, AccessPolicy access,
                                           ResetPolicy reset,
                                           void (*harvest)(void*, MetricRecord&) noexcept,
                                           void (*destroy)(void*) noexcept, void*& object_out,
                                           Slot_meta*& meta_out) {
    if (!alive_.load(std::memory_order_acquire)) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    // An instrument name is unique inside its meter (across kinds too).
    if (has_duplicate(identity, detail::view_name(identity.metric_name))) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    Kind_pool& pool = pools_[kind_index(kind)];
    Slot_meta* meta = nullptr;
    for (std::uint32_t i = 0; i < pool.slot_max; ++i) {
        if (!pool.meta[i].occupied) {
            meta = &pool.meta[i];
            break;
        }
    }
    if (meta == nullptr) {
        return std::unexpected(core::ErrorCode::BufferFull);
    }
    const std::size_t bytes = detail::align_up(object_size);
    if (bytes > pool.arena_size || pool.used + bytes > pool.arena_size) {
        return std::unexpected(core::ErrorCode::BufferFull);
    }
    void* object = pool.arena + pool.used;
    pool.used += bytes;
    meta->identity = identity;
    meta->kind = kind;
    meta->concurrency = concurrency;
    meta->access = access;
    meta->reset = reset;
    meta->object = object;
    meta->harvest = harvest;
    meta->destroy = destroy;
    meta->occupied = true;
    object_out = object;
    meta_out = meta;
    return {};
}

bool MetricProvider::has_duplicate(const MetricIdentity& meter_identity,
                                   std::string_view name) const noexcept {
    for (const auto& pool : pools_) {
        for (std::uint32_t i = 0; i < pool.slot_max; ++i) {
            const Slot_meta& meta = pool.meta[i];
            if (!meta.occupied) {
                continue;
            }
            if (detail::view_name(meta.identity.meter_name) !=
                    detail::view_name(meter_identity.meter_name) ||
                detail::view_name(meta.identity.meter_version) !=
                    detail::view_name(meter_identity.meter_version)) {
                continue;
            }
            if (detail::view_name(meta.identity.metric_name) == name) {
                return true;
            }
        }
    }
    return false;
}

core::Status MetricProvider::collect_now() noexcept {
    std::lock_guard<std::mutex> lock(harvest_mutex_);
    if (!alive_.load(std::memory_order_acquire)) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    std::uint32_t count = 0;
    for (auto& pool : pools_) {
        for (std::uint32_t i = 0; i < pool.slot_max; ++i) {
            Slot_meta& meta = pool.meta[i];
            if (!meta.occupied || meta.harvest == nullptr) {
                continue;
            }
            if (count >= record_capacity_) {
                break;
            }
            MetricRecord& record = records_[count];
            record.identity = meta.identity;
            record.kind = meta.kind;
            record.concurrency = meta.concurrency;
            record.access = meta.access;
            record.reset = meta.reset;
            record.scalar = ScalarValue{};
            record.statistics = StatisticsSnapshot{};
            reset_histogram_header(record.histogram);
            meta.harvest(meta.object, record);
            ++count;
        }
    }

    chrono::Timestamp stamp{};
    const core::Result<chrono::Timestamp> now = chrono::Timestamp::now();
    if (now) {
        stamp = *now;
    }
    std::int64_t offset_ns = 0;
    if (chrono::sample_utc_offset(offset_ns)) {
        utc_offset_ns_ = offset_ns;
    }

    MetricBatchSnapshot batch{};
    batch.stamp = stamp;
    batch.utc_ns = chrono::utc_from_mono_ns(stamp.nanoseconds(), utc_offset_ns_);
    batch.records = records_;
    batch.record_count = count;
    batch.record_capacity = record_capacity_;

    if (binding_.on_batch == nullptr) {
        return {};
    }
    return binding_.on_batch(binding_.self, batch);
}

core::Status MetricProvider::flush() noexcept {
    if (binding_.flush == nullptr) {
        return {};
    }
    return binding_.flush(binding_.self);
}

core::Status MetricProvider::startup_status() const noexcept {
    const core::ErrorCode error = startup_error_.load(std::memory_order_acquire);
    if (error != core::ErrorCode::Ok) {
        return std::unexpected(error);
    }
    return {};
}

std::string MetricProvider::to_string() const {
    std::string out = "MetricProvider(collect_interval_ns=" +
                      std::to_string(config_.collect_interval.count()) +
                      ", core_mask=0x" + std::to_string(config_.core_mask) +
                      ", meters=" + std::to_string(meter_capacity_) +
                      ", records=" + std::to_string(record_capacity_) +
                      ", block_bytes=" + std::to_string(block_size_) + ")";
    return out;
}

void MetricProvider::harvest_loop(std::stop_token stop) noexcept {
    // Pin this thread before the first batch: the harvester must not preempt
    // the real-time threads.
    if (config_.core_mask != 0U) {
        const core::Status pinned = detail::pin_self(config_.core_mask);
        if (!pinned) {
            startup_error_.store(pinned.error(), std::memory_order_release);
        }
    }

    auto next = std::chrono::steady_clock::now() + config_.collect_interval;
    while (!stop.stop_requested()) {
        {
            std::unique_lock<std::mutex> lock(wake_mutex_);
            wake_cv_.wait_until(lock, stop, next, [] { return false; });
        }
        if (stop.stop_requested()) {
            break;
        }
        static_cast<void>(collect_now());
        next += config_.collect_interval;
        const auto now = std::chrono::steady_clock::now();
        if (next < now) {
            next = now + config_.collect_interval; // do not chase a backlog
        }
    }
}

} // namespace nexigen::metrics
