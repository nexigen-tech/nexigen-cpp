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

/// @brief Online moment summary of a `DescriptiveStatistics` instrument.
///
/// Holds the running count and the minimum, maximum, mean, and population
/// standard deviation maintained without storing samples, plus counters for
/// observations the instrument intentionally excluded. Fits in the
/// `MetricRecord` payload union and stays trivially copyable.
///
/// @note Spec `metrics/descriptive-statistics`, requirement "Online moment
///       statistics".
struct StatisticsSnapshot {
    std::uint64_t count{0};           ///< Accepted observations in the window.
    double min{0.0};                  ///< Smallest accepted observation.
    double max{0.0};                  ///< Largest accepted observation.
    double mean{0.0};                 ///< Running arithmetic mean.
    double stddev{0.0};               ///< Population standard deviation.
    std::uint64_t ignored_samples{0}; ///< Non-finite inputs, excluded from moments.
    std::uint64_t dropped_samples{0}; ///< Samples lost to an exhausted thread pool.

    /// @brief Render the summary as a cold-path diagnostic string.
    ///
    /// @return Human-readable text carrying the moments and sample counters.
    /// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
    ///       strings"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

static_assert(std::is_trivially_copyable_v<StatisticsSnapshot>);

/// @brief Free diagnostic helper mirroring `StatisticsSnapshot::to_string()`.
///
/// @param snapshot Summary to render.
/// @return The same text the member returns.
/// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
///       strings"; diagnostic only (project mandate 9).
[[nodiscard]] std::string to_string(const StatisticsSnapshot& snapshot);

/// @brief Write a statistics summary as JSON (cold path).
///
/// @param j Output JSON object with one field per moment and counter.
/// @param snapshot Summary to serialize.
/// @note Spec `metrics/descriptive-statistics`, requirement "Statistics
///       serialization contract"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const StatisticsSnapshot& snapshot);
/// @brief Parse a statistics summary from JSON without throwing.
///
/// Reads each numeric field through typed guards and ignores missing or
/// incompatible fields.
///
/// @param j JSON object to read.
/// @param out Destination, fields updated independently.
/// @note Spec `metrics/snapshot-format`, requirement "Statistics and histogram
///       snapshots use typed numeric readers".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, StatisticsSnapshot& out) noexcept;

} // namespace nexigen::metrics

/// @brief `std::formatter` for `StatisticsSnapshot`, rendering its diagnostics.
///
/// @note Project mandate 11; formatting is cold path only.
template <>
struct std::formatter<nexigen::metrics::StatisticsSnapshot> : std::formatter<std::string_view> {
    /// @brief Format the summary through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param value Summary to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::metrics::StatisticsSnapshot& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
