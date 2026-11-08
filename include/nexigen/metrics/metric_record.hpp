// nexigen::metrics — one harvested metric (design D12; spec
// metrics/metric-provider "Record and batch snapshot serialization").
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/metrics/histogram_snapshot.hpp>
#include <nexigen/metrics/metric_identity.hpp>
#include <nexigen/metrics/policy_enums.hpp>
#include <nexigen/metrics/scalar_value.hpp>
#include <nexigen/metrics/state_value.hpp>
#include <nexigen/metrics/statistics_snapshot.hpp>

namespace nexigen::metrics {

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
        StateValue state;
    };

    [[nodiscard]] std::string to_string() const;
};

static_assert(std::is_trivially_copyable_v<MetricRecord>);

[[nodiscard]] std::string to_string(const MetricRecord& record);

void to_json(nlohmann::json& j, const MetricRecord& record);
void from_json(const nlohmann::json& j, MetricRecord& out) noexcept;

} // namespace nexigen::metrics

/// MetricRecord formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::MetricRecord> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::metrics::MetricRecord& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
