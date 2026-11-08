// nexigen::metrics — online moment summary of a DescriptiveStatistics
// instrument (design D12; spec metrics/metric-provider).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace nexigen::metrics {

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

static_assert(std::is_trivially_copyable_v<StatisticsSnapshot>);

[[nodiscard]] std::string to_string(const StatisticsSnapshot& snapshot);

void to_json(nlohmann::json& j, const StatisticsSnapshot& snapshot);
void from_json(const nlohmann::json& j, StatisticsSnapshot& out) noexcept;

} // namespace nexigen::metrics

/// StatisticsSnapshot formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::StatisticsSnapshot> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::StatisticsSnapshot& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
