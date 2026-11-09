// nexigen::metrics — MetricKind enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
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

} // namespace nexigen::metrics

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::metrics::MetricKind> {
    struct Entry {
        nexigen::metrics::MetricKind value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 6> entries{{
        {nexigen::metrics::MetricKind::Counter, "Counter"},
        {nexigen::metrics::MetricKind::Gauge, "Gauge"},
        {nexigen::metrics::MetricKind::DescriptiveStatistics, "DescriptiveStatistics"},
        {nexigen::metrics::MetricKind::ExplicitHistogram, "ExplicitHistogram"},
        {nexigen::metrics::MetricKind::ExponentialHistogram, "ExponentialHistogram"},
        {nexigen::metrics::MetricKind::State, "State"},
    }};
};

} // namespace nexigen::core

namespace nexigen::metrics {

[[nodiscard]] constexpr std::string_view to_string(MetricKind kind) noexcept {
    return core::enum_name(kind);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, MetricKind& out) noexcept {
    return core::enum_value(name, out);
}

inline void from_json(const nlohmann::json& j, MetricKind& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

inline void to_json(nlohmann::json& j, const MetricKind metricKind) {
    core::mandate::enum_to_json(j, metricKind);
}

} // namespace nexigen::metrics
