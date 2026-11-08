// nexigen::metrics — MetricKind enum.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::metrics {

/// Which instrument family a registry slot holds.
enum class MetricKind : std::uint8_t {
    Counter,
    Gauge,
    DescriptiveStatistics,
    ExplicitHistogram,
    ExponentialHistogram,
    State,
};

[[nodiscard]] constexpr std::string_view to_string(MetricKind kind) noexcept {
    switch (kind) {
        case MetricKind::Counter:               return "Counter";
        case MetricKind::Gauge:                 return "Gauge";
        case MetricKind::DescriptiveStatistics: return "DescriptiveStatistics";
        case MetricKind::ExplicitHistogram:     return "ExplicitHistogram";
        case MetricKind::ExponentialHistogram:  return "ExponentialHistogram";
        case MetricKind::State:                 return "State";
        default:                                return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name, MetricKind& out) noexcept {
    if (name == "Counter") {
        out = MetricKind::Counter;
        return true;
    }
    if (name == "Gauge") {
        out = MetricKind::Gauge;
        return true;
    }
    if (name == "DescriptiveStatistics") {
        out = MetricKind::DescriptiveStatistics;
        return true;
    }
    if (name == "ExplicitHistogram") {
        out = MetricKind::ExplicitHistogram;
        return true;
    }
    if (name == "ExponentialHistogram") {
        out = MetricKind::ExponentialHistogram;
        return true;
    }
    if (name == "State") {
        out = MetricKind::State;
        return true;
    }
    return false;
}

inline void from_json(const nlohmann::json& j, MetricKind& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}


inline void to_json(nlohmann::json& j, const MetricKind metricKind) {
    j = std::string(to_string(metricKind));
}

} // namespace nexigen::metrics
