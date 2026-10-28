// nexigen::metrics — snapshot and record value types (design D12;
// spec metrics/metric-provider "Record and batch snapshot serialization").
//
// Everything the provider hands to a sink is a fixed-size, trivially copyable
// value: names live in inline char arrays (no std::string, no heap), the
// histogram payload is an inline bucket table, and the batch carries a pointer
// to the provider-owned record buffer instead of copying it. MetricRecord
// carries its payload in a union sized to the largest variant.
//
// COLD PATH ONLY: to_string / to_json / from_json allocate (std::string,
// nlohmann::json) and are diagnostics. The recording and harvesting paths
// never call them (spec metrics/policy-enums "Serialization stays off the hot
// path"). Their bodies live in src/metrics/snapshot_format.cpp (design D2);
// only the declarations and the header-inline std::formatter adapters remain
// here so consumer TUs stay lean.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics {

/// Maximum length of a meter name, meter version or metric name (chars; the
/// stored buffer holds one more byte for the NUL).
inline constexpr std::size_t max_name_length = 63;

/// Maximum number of buckets carried by a histogram snapshot.
inline constexpr std::size_t max_snapshot_buckets = 256;

namespace detail {

/// Copy a name into a fixed NUL-terminated buffer. Returns false when the
/// name is empty or longer than max_name_length (the caller maps that to
/// core::ErrorCode::InvalidArgument). Never allocates.
[[nodiscard]] inline bool copy_name(char (&dst)[max_name_length + 1],
                                    std::string_view name) noexcept {
    if (name.empty() || name.size() > max_name_length) {
        return false;
    }
    for (std::size_t i = 0; i < name.size(); ++i) {
        dst[i] = name[i];
    }
    dst[name.size()] = '\0';
    return true;
}

/// Copy a stored name back out as a view (stopping at the first NUL).
[[nodiscard]] inline std::string_view view_name(const char (&src)[max_name_length + 1]) noexcept {
    std::size_t len = 0;
    while (len < max_name_length + 1 && src[len] != '\0') {
        ++len;
    }
    return std::string_view(src, len);
}

/// Copy a stored name field verbatim (including its NUL).
inline void copy_field(char (&dst)[max_name_length + 1],
                       const char (&src)[max_name_length + 1]) noexcept {
    for (std::size_t i = 0; i <= max_name_length; ++i) {
        dst[i] = src[i];
    }
}

} // namespace detail

/// Who produced a record: the meter that owns the instrument.
struct MetricIdentity {
    char meter_name[max_name_length + 1]{};
    char meter_version[max_name_length + 1]{};
    char metric_name[max_name_length + 1]{};

    [[nodiscard]] std::string to_string() const;
};

namespace detail {

/// Copy the meter part of an identity (name + version).
inline void copy_meter_identity(MetricIdentity& to, const MetricIdentity& from) noexcept {
    copy_field(to.meter_name, from.meter_name);
    copy_field(to.meter_version, from.meter_version);
}

} // namespace detail

/// Scalar payload of a counter or a gauge.
struct ScalarValue {
    std::uint64_t counter{0};   ///< Counter total / window delta
    std::int64_t gauge_int{0};  ///< Gauge<std::int64_t>
    double gauge_double{0.0};   ///< Gauge<double>
    bool is_double{false};      ///< true when the gauge carries a double

    [[nodiscard]] std::string to_string() const;
};

/// Online moment summary of a DescriptiveStatistics instrument.
struct StatisticsSnapshot {
    std::uint64_t count{0};
    double min{0.0};
    double max{0.0};
    double mean{0.0};
    double stddev{0.0};
    std::uint64_t ignored_samples{0}; ///< non-finite inputs, excluded from moments
    std::uint64_t dropped_samples{0}; ///< samples lost to an exhausted thread pool

    [[nodiscard]] std::string to_string() const;
};

/// Bucket table plus summary and percentiles of a histogram instrument.
struct HistogramSnapshot {
    std::uint64_t count{0};
    double sum{0.0};
    double min{0.0};
    double max{0.0};
    std::uint64_t ignored_samples{0}; ///< rejected (non-finite / negative) inputs
    std::uint64_t zero_count{0};      ///< zero samples (rank below every bucket)
    std::int32_t effective_scale{0};  ///< scale in force (may be coarser than configured)
    double p50{0.0};
    double p90{0.0};
    double p99{0.0};
    double p999{0.0};
    std::uint32_t bucket_count{0};
    double buckets_lower[max_snapshot_buckets]{};
    std::uint64_t buckets_count[max_snapshot_buckets]{};

    [[nodiscard]] std::string to_string() const;
};

/// One harvested metric: identity, classification and the kind-specific
/// payload (a union sized to the largest variant; `kind` selects the member).
struct MetricRecord {
    MetricIdentity identity{};
    MetricKind kind{MetricKind::Counter};
    ConcurrencyPolicy concurrency{ConcurrencyPolicy::SingleThreaded};
    AccessPolicy access{AccessPolicy::Readable};
    ResetPolicy reset{ResetPolicy::Cumulative};
    union {
        ScalarValue scalar{};
        StatisticsSnapshot statistics;
        HistogramSnapshot histogram;
    };

    [[nodiscard]] std::string to_string() const;
};

/// One collection window: the stamp plus the harvested records.
///
/// The record buffer is owned by the provider (a pre-allocated, cache-line
/// aligned pool); the batch only points at it. from_json fills a
/// caller-provided buffer, bounded by record_capacity.
struct MetricBatchSnapshot {
    chrono::Timestamp stamp{};
    std::int64_t utc_ns{0};
    MetricRecord* records{nullptr};
    std::uint32_t record_count{0};
    std::uint32_t record_capacity{0};

    [[nodiscard]] std::string to_string() const;
};

// Design D12 pins: every snapshot value is fixed-size and trivially copyable.
static_assert(std::is_trivially_copyable_v<MetricIdentity>);
static_assert(std::is_trivially_copyable_v<ScalarValue>);
static_assert(std::is_trivially_copyable_v<StatisticsSnapshot>);
static_assert(std::is_trivially_copyable_v<HistogramSnapshot>);
static_assert(std::is_trivially_copyable_v<MetricRecord>);
static_assert(std::is_trivially_copyable_v<MetricBatchSnapshot>);

// --- free helpers (mandate 9): mirror the member functions.

[[nodiscard]] std::string to_string(const MetricIdentity& identity);
[[nodiscard]] std::string to_string(const ScalarValue& value);
[[nodiscard]] std::string to_string(const StatisticsSnapshot& snapshot);
[[nodiscard]] std::string to_string(const HistogramSnapshot& snapshot);
[[nodiscard]] std::string to_string(const MetricRecord& record);
[[nodiscard]] std::string to_string(const MetricBatchSnapshot& batch);

// --- nlohmann::json (cold path). Readers are strictly non-throwing under
// JSON_NOEXCEPTION=1: is_* checks guard every access, and a wrong shape
// leaves the target untouched instead of aborting. Definitions live in
// src/metrics/snapshot_format.cpp.

void to_json(nlohmann::json& j, const MetricIdentity& identity);
void from_json(const nlohmann::json& j, MetricIdentity& out) noexcept;

void to_json(nlohmann::json& j, const ScalarValue& value);
void from_json(const nlohmann::json& j, ScalarValue& out) noexcept;

void to_json(nlohmann::json& j, const StatisticsSnapshot& snapshot);
void from_json(const nlohmann::json& j, StatisticsSnapshot& out) noexcept;

void to_json(nlohmann::json& j, const HistogramSnapshot& snapshot);
void from_json(const nlohmann::json& j, HistogramSnapshot& out) noexcept;

void to_json(nlohmann::json& j, const MetricRecord& record);
void from_json(const nlohmann::json& j, MetricRecord& out) noexcept;

void to_json(nlohmann::json& j, const MetricBatchSnapshot& batch);
void from_json(const nlohmann::json& j, MetricBatchSnapshot& out) noexcept;

} // namespace nexigen::metrics

/// Snapshot types format as their diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::MetricIdentity> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::MetricIdentity& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

template <>
struct std::formatter<nexigen::metrics::ScalarValue> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::ScalarValue& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

template <>
struct std::formatter<nexigen::metrics::StatisticsSnapshot> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::StatisticsSnapshot& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

template <>
struct std::formatter<nexigen::metrics::HistogramSnapshot> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::HistogramSnapshot& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

template <>
struct std::formatter<nexigen::metrics::MetricRecord> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::MetricRecord& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

template <>
struct std::formatter<nexigen::metrics::MetricBatchSnapshot> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::MetricBatchSnapshot& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
