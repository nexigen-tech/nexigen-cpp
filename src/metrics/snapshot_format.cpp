// nexigen::metrics — cold-path snapshot/record serialization (design D2/D12;
// spec metrics/metric-provider "Record and batch snapshot serialization").
//
// Bodies for the to_string / to_json / from_json contract declared in
// include/nexigen/metrics/metric_snapshot.hpp. Everything here allocates
// (std::string, nlohmann::json) and is diagnostics-only: no recording or
// harvesting path calls into this unit.

#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/metric_provider.hpp>

#include <algorithm>
#include <string>

namespace nexigen::metrics {

// --- member to_string (mandate 9).

std::string MetricIdentity::to_string() const {
    return "MetricIdentity(meter=" + std::string(detail::view_name(meter_name)) +
           ", version=" + std::string(detail::view_name(meter_version)) +
           ", metric=" + std::string(detail::view_name(metric_name)) + ")";
}

std::string ScalarValue::to_string() const {
    std::string out = "ScalarValue(";
    out += is_double ? ("double=" + std::to_string(gauge_double))
                     : ("int64=" + std::to_string(gauge_int));
    out += ", counter=" + std::to_string(counter);
    out += ")";
    return out;
}

std::string StatisticsSnapshot::to_string() const {
    return "StatisticsSnapshot(count=" + std::to_string(count) +
           ", min=" + std::to_string(min) +
           ", max=" + std::to_string(max) +
           ", mean=" + std::to_string(mean) +
           ", std=" + std::to_string(stddev) +
           ", ignored=" + std::to_string(ignored_samples) +
           ", dropped=" + std::to_string(dropped_samples) + ")";
}

std::string HistogramSnapshot::to_string() const {
    return "HistogramSnapshot(count=" + std::to_string(count) +
           ", sum=" + std::to_string(sum) +
           ", min=" + std::to_string(min) +
           ", max=" + std::to_string(max) +
           ", zeros=" + std::to_string(zero_count) +
           ", scale=" + std::to_string(effective_scale) +
           ", p50=" + std::to_string(p50) +
           ", p90=" + std::to_string(p90) +
           ", p99=" + std::to_string(p99) +
           ", p999=" + std::to_string(p999) +
           ", buckets=" + std::to_string(bucket_count) + ")";
}

std::string MetricRecord::to_string() const {
    std::string out = "MetricRecord(";
    out += identity.to_string();
    out += ", kind=" + std::string(metrics::to_string(kind));
    out += ", concurrency=" + std::string(metrics::to_string(concurrency));
    out += ", access=" + std::string(metrics::to_string(access));
    out += ", reset=" + std::string(metrics::to_string(reset));
    out += ", values=";
    switch (kind) {
        case MetricKind::Counter:
        case MetricKind::Gauge:
            out += scalar.to_string();
            break;
        case MetricKind::DescriptiveStatistics:
            out += statistics.to_string();
            break;
        default:
            out += histogram.to_string();
            break;
    }
    out += ")";
    return out;
}

std::string MetricBatchSnapshot::to_string() const {
    std::string out = "MetricBatchSnapshot(stamp_ns=" + std::to_string(stamp.nanoseconds()) +
                      ", utc_ns=" + std::to_string(utc_ns) +
                      ", records=" + std::to_string(record_count) + ")";
    return out;
}

// --- free helpers (mandate 9): mirror the member functions.

std::string to_string(const MetricIdentity& identity) { return identity.to_string(); }
std::string to_string(const ScalarValue& value) { return value.to_string(); }
std::string to_string(const StatisticsSnapshot& snapshot) { return snapshot.to_string(); }
std::string to_string(const HistogramSnapshot& snapshot) { return snapshot.to_string(); }
std::string to_string(const MetricRecord& record) { return record.to_string(); }
std::string to_string(const MetricBatchSnapshot& batch) { return batch.to_string(); }

// --- nlohmann::json (cold path). Readers are strictly non-throwing under
// JSON_NOEXCEPTION=1: is_* checks guard every access, and a wrong shape
// leaves the target untouched instead of aborting.

void to_json(nlohmann::json& j, const MetricIdentity& identity) {
    j = nlohmann::json{
        {"meter_name", std::string(detail::view_name(identity.meter_name))},
        {"meter_version", std::string(detail::view_name(identity.meter_version))},
        {"metric_name", std::string(detail::view_name(identity.metric_name))},
    };
}

void from_json(const nlohmann::json& j, MetricIdentity& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto read = [](const nlohmann::json& node, const char* key, char (&dst)[max_name_length + 1]) {
        const auto it = node.find(key);
        if (it == node.end() || !it->is_string()) {
            return;
        }
        const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
        if (text != nullptr) {
            static_cast<void>(detail::copy_name(dst, std::string_view(*text)));
        }
    };
    read(j, "meter_name", out.meter_name);
    read(j, "meter_version", out.meter_version);
    read(j, "metric_name", out.metric_name);
}

void to_json(nlohmann::json& j, const ScalarValue& value) {
    j = nlohmann::json{
        {"counter", value.counter},
        {"gauge_int", value.gauge_int},
        {"gauge_double", value.gauge_double},
        {"is_double", value.is_double},
    };
}

void from_json(const nlohmann::json& j, ScalarValue& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto number = [&j](const char* key, double& target) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number()) {
            target = it->get<double>();
        }
    };
    const auto unsigned_number = [&j](const char* key, std::uint64_t& target) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_unsigned()) {
            target = it->get<std::uint64_t>();
        } else if (it != j.end() && it->is_number_integer()) {
            const auto signed_value = it->get<std::int64_t>();
            if (signed_value > 0) {
                target = static_cast<std::uint64_t>(signed_value);
            }
        }
    };
    const auto signed_number = [&j](const char* key, std::int64_t& target) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_integer()) {
            target = it->get<std::int64_t>();
        }
    };
    unsigned_number("counter", out.counter);
    signed_number("gauge_int", out.gauge_int);
    number("gauge_double", out.gauge_double);
    const auto it = j.find("is_double");
    if (it != j.end() && it->is_boolean()) {
        out.is_double = it->get<bool>();
    }
}

void to_json(nlohmann::json& j, const StatisticsSnapshot& snapshot) {
    j = nlohmann::json{
        {"count", snapshot.count},
        {"min", snapshot.min},
        {"max", snapshot.max},
        {"mean", snapshot.mean},
        {"std", snapshot.stddev},
        {"ignored_samples", snapshot.ignored_samples},
        {"dropped_samples", snapshot.dropped_samples},
    };
}

void from_json(const nlohmann::json& j, StatisticsSnapshot& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto number = [&j](const char* key, double& target) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number()) {
            target = it->get<double>();
        }
    };
    const auto unsigned_number = [&j](const char* key, std::uint64_t& target) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_unsigned()) {
            target = it->get<std::uint64_t>();
        }
    };
    unsigned_number("count", out.count);
    number("min", out.min);
    number("max", out.max);
    number("mean", out.mean);
    number("std", out.stddev);
    unsigned_number("ignored_samples", out.ignored_samples);
    unsigned_number("dropped_samples", out.dropped_samples);
}

void to_json(nlohmann::json& j, const HistogramSnapshot& snapshot) {
    nlohmann::json buckets = nlohmann::json::array();
    nlohmann::json counts = nlohmann::json::array();
    for (std::uint32_t i = 0; i < snapshot.bucket_count && i < max_snapshot_buckets; ++i) {
        buckets.push_back(snapshot.buckets_lower[i]);
        counts.push_back(snapshot.buckets_count[i]);
    }
    j = nlohmann::json{
        {"count", snapshot.count},
        {"sum", snapshot.sum},
        {"min", snapshot.min},
        {"max", snapshot.max},
        {"ignored_samples", snapshot.ignored_samples},
        {"zero_count", snapshot.zero_count},
        {"effective_scale", snapshot.effective_scale},
        {"p50", snapshot.p50},
        {"p90", snapshot.p90},
        {"p99", snapshot.p99},
        {"p99.9", snapshot.p999},
        {"bucket_count", snapshot.bucket_count},
        {"buckets_lower", buckets},
        {"buckets_count", counts},
    };
}

void from_json(const nlohmann::json& j, HistogramSnapshot& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto number = [&j](const char* key, double& target) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number()) {
            target = it->get<double>();
        }
    };
    const auto unsigned_number = [&j](const char* key, std::uint64_t& target) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_unsigned()) {
            target = it->get<std::uint64_t>();
        }
    };
    unsigned_number("count", out.count);
    number("sum", out.sum);
    number("min", out.min);
    number("max", out.max);
    unsigned_number("ignored_samples", out.ignored_samples);
    unsigned_number("zero_count", out.zero_count);
    number("p50", out.p50);
    number("p90", out.p90);
    number("p99", out.p99);
    number("p99.9", out.p999);
    const auto scale_it = j.find("effective_scale");
    if (scale_it != j.end() && scale_it->is_number_integer()) {
        out.effective_scale = scale_it->get<std::int32_t>();
    }
    const auto count_it = j.find("bucket_count");
    if (count_it != j.end() && count_it->is_number_unsigned()) {
        out.bucket_count = std::min<std::uint32_t>(count_it->get<std::uint32_t>(),
                                                    static_cast<std::uint32_t>(max_snapshot_buckets));
    }
    const auto lower_it = j.find("buckets_lower");
    if (lower_it != j.end() && lower_it->is_array()) {
        std::uint32_t i = 0;
        for (const auto& entry : *lower_it) {
            if (i >= out.bucket_count || i >= max_snapshot_buckets || !entry.is_number()) {
                break;
            }
            out.buckets_lower[i] = entry.get<double>();
            ++i;
        }
    }
    const auto counts_it = j.find("buckets_count");
    if (counts_it != j.end() && counts_it->is_array()) {
        std::uint32_t i = 0;
        for (const auto& entry : *counts_it) {
            if (i >= out.bucket_count || i >= max_snapshot_buckets || !entry.is_number_unsigned()) {
                break;
            }
            out.buckets_count[i] = entry.get<std::uint64_t>();
            ++i;
        }
    }
}

void to_json(nlohmann::json& j, const MetricRecord& record) {
    j = nlohmann::json{
        {"identity", record.identity},
        {"kind", std::string(metrics::to_string(record.kind))},
        {"concurrency", std::string(metrics::to_string(record.concurrency))},
        {"access", std::string(metrics::to_string(record.access))},
        {"reset", std::string(metrics::to_string(record.reset))},
    };
    // The payload is a union: only the member selected by `kind` is active.
    switch (record.kind) {
        case MetricKind::Counter:
        case MetricKind::Gauge:
            j["scalar"] = record.scalar;
            break;
        case MetricKind::DescriptiveStatistics:
            j["statistics"] = record.statistics;
            break;
        default:
            j["histogram"] = record.histogram;
            break;
    }
}

void from_json(const nlohmann::json& j, MetricRecord& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto it = j.find("identity");
    if (it != j.end()) {
        from_json(*it, out.identity);
    }
    const auto read_enum = [&j](const char* key, auto& target) {
        const auto node = j.find(key);
        if (node == j.end() || !node->is_string()) {
            return;
        }
        const auto* const text = node->get_ptr<const nlohmann::json::string_t*>();
        if (text != nullptr) {
            static_cast<void>(metrics::from_string(std::string_view(*text), target));
        }
    };
    read_enum("kind", out.kind);
    read_enum("concurrency", out.concurrency);
    read_enum("access", out.access);
    read_enum("reset", out.reset);
    // Deserialize into the union member selected by `kind`.
    switch (out.kind) {
        case MetricKind::Counter:
        case MetricKind::Gauge: {
            const auto scalar_it = j.find("scalar");
            if (scalar_it != j.end()) {
                from_json(*scalar_it, out.scalar);
            }
            break;
        }
        case MetricKind::DescriptiveStatistics: {
            const auto stats_it = j.find("statistics");
            if (stats_it != j.end()) {
                from_json(*stats_it, out.statistics);
            }
            break;
        }
        default: {
            const auto hist_it = j.find("histogram");
            if (hist_it != j.end()) {
                from_json(*hist_it, out.histogram);
            }
            break;
        }
    }
}

void to_json(nlohmann::json& j, const MetricBatchSnapshot& batch) {
    nlohmann::json records = nlohmann::json::array();
    for (std::uint32_t i = 0; i < batch.record_count; ++i) {
        if (batch.records == nullptr) {
            break;
        }
        records.push_back(batch.records[i]);
    }
    j = nlohmann::json{
        {"stamp_ns", batch.stamp.nanoseconds()},
        {"utc_ns", batch.utc_ns},
        {"record_count", batch.record_count},
        {"records", records},
    };
}

void from_json(const nlohmann::json& j, MetricBatchSnapshot& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto stamp_it = j.find("stamp_ns");
    if (stamp_it != j.end() && stamp_it->is_number_unsigned()) {
        out.stamp = chrono::Timestamp{stamp_it->get<std::uint64_t>()};
    }
    const auto utc_it = j.find("utc_ns");
    if (utc_it != j.end() && utc_it->is_number_integer()) {
        out.utc_ns = utc_it->get<std::int64_t>();
    }
    const auto count_it = j.find("record_count");
    if (count_it != j.end() && count_it->is_number_unsigned()) {
        out.record_count = count_it->get<std::uint32_t>();
    }
    const auto records_it = j.find("records");
    if (records_it == j.end() || !records_it->is_array() || out.records == nullptr) {
        return;
    }
    std::uint32_t i = 0;
    for (const auto& entry : *records_it) {
        if (i >= out.record_capacity) {
            break;
        }
        from_json(entry, out.records[i]);
        ++i;
    }
    if (i < out.record_count) {
        out.record_count = i;
    }
}

} // namespace nexigen::metrics

// --- MetricProvider_config (design D2: record/batch/config serialization
// lives in this unit).

std::string nexigen::metrics::MetricProvider_config::to_string() const {
    return "MetricProvider_config(collect_interval_ns=" +
           std::to_string(collect_interval.count()) +
           ", core_mask=0x" + std::to_string(core_mask) +
           ", max_meters=" + std::to_string(max_meters) +
           ", max_counters=" + std::to_string(max_counters) +
           ", max_gauges=" + std::to_string(max_gauges) +
           ", max_statistics=" + std::to_string(max_statistics) +
           ", max_explicit_histograms=" + std::to_string(max_explicit_histograms) +
           ", max_exponential_histograms=" + std::to_string(max_exponential_histograms) + ")";
}

namespace nexigen::metrics {

std::string to_string(const MetricProvider_config& config) { return config.to_string(); }

void to_json(nlohmann::json& j, const MetricProvider_config& config) {
    j = nlohmann::json{
        {"collect_interval_ns", config.collect_interval.count()},
        {"core_mask", config.core_mask},
        {"max_meters", config.max_meters},
        {"max_counters", config.max_counters},
        {"max_gauges", config.max_gauges},
        {"max_statistics", config.max_statistics},
        {"max_explicit_histograms", config.max_explicit_histograms},
        {"max_exponential_histograms", config.max_exponential_histograms},
    };
}

void from_json(const nlohmann::json& j, MetricProvider_config& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto number = [&j](const char* key, std::int64_t& target) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_integer()) {
            target = it->get<std::int64_t>();
        }
    };
    const auto unsigned_number = [&j](const char* key, std::uint32_t& target) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_unsigned()) {
            target = it->get<std::uint32_t>();
        }
    };
    std::int64_t interval = out.collect_interval.count();
    number("collect_interval_ns", interval);
    out.collect_interval = std::chrono::nanoseconds{interval};
    const auto mask_it = j.find("core_mask");
    if (mask_it != j.end() && mask_it->is_number_unsigned()) {
        out.core_mask = mask_it->get<std::uint64_t>();
    }
    unsigned_number("max_meters", out.max_meters);
    unsigned_number("max_counters", out.max_counters);
    unsigned_number("max_gauges", out.max_gauges);
    unsigned_number("max_statistics", out.max_statistics);
    unsigned_number("max_explicit_histograms", out.max_explicit_histograms);
    unsigned_number("max_exponential_histograms", out.max_exponential_histograms);
}

} // namespace nexigen::metrics
