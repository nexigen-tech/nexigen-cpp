// nexigen::metrics — one collection window of harvested records (design D12;
// spec metrics/metric-provider).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/metrics/metric_record.hpp>

namespace nexigen::metrics {

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

static_assert(std::is_trivially_copyable_v<MetricBatchSnapshot>);

[[nodiscard]] std::string to_string(const MetricBatchSnapshot& batch);

void to_json(nlohmann::json& j, const MetricBatchSnapshot& batch);
void from_json(const nlohmann::json& j, MetricBatchSnapshot& out) noexcept;

} // namespace nexigen::metrics

/// MetricBatchSnapshot formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::MetricBatchSnapshot> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::MetricBatchSnapshot& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
