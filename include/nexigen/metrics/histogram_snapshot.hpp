// nexigen::metrics — bucket table plus summary and percentiles of a histogram
// instrument (design D12; spec metrics/metric-provider).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/metrics/metric_identity.hpp>

namespace nexigen::metrics {

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

static_assert(std::is_trivially_copyable_v<HistogramSnapshot>);

[[nodiscard]] std::string to_string(const HistogramSnapshot& snapshot);

void to_json(nlohmann::json& j, const HistogramSnapshot& snapshot);
void from_json(const nlohmann::json& j, HistogramSnapshot& out) noexcept;

} // namespace nexigen::metrics

/// HistogramSnapshot formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::HistogramSnapshot> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::HistogramSnapshot& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
