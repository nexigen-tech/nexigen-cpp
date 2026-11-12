// nexigen::metrics — provider configuration (design D14; spec
// metrics/metric-provider).
#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>

namespace nexigen::metrics {

/// @brief Provider configuration: collection period, affinity, and capacities.
///
/// A zero `collect_interval` disables the background harvester, leaving
/// `collect_now()` as the only trigger. Capacities are reserved once at
/// initialization, so the registry never grows during recording or harvesting.
/// The type is a plain value aggregate; the provider copies it at construction.
///
/// @note Spec `metrics/metric-provider`, requirement "Background harvesting and
///       collection period".
/// @note Spec `metrics/metric-provider`, requirement "Pre-allocated aligned
///       metric registry".
struct MetricProviderConfig {
    std::chrono::nanoseconds collect_interval{0}; ///< Harvest period; zero disables.
    core::ThreadSpec worker_thread{};             ///< Harvester thread name/affinity/scheduling.
    std::uint32_t max_meters{16};                 ///< Meter capacity.
    std::uint32_t max_counters{64};               ///< Counter capacity.
    std::uint32_t max_gauges{64};                 ///< Gauge capacity.
    std::uint32_t max_states{16};                 ///< State-gauge capacity.
    std::uint32_t max_statistics{32};             ///< Descriptive-statistics capacity.
    std::uint32_t max_explicit_histograms{16};    ///< Explicit-histogram capacity.
    std::uint32_t max_exponential_histograms{8};  ///< Exponential-histogram capacity.

    /// @brief Render the configuration as a cold-path diagnostic string.
    ///
    /// @return Text describing the interval, worker spec, and capacities.
    /// @note Spec `metrics/metric-provider`, requirement "Provider configuration
    ///       serialization"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `MetricProviderConfig::to_string()`.
///
/// @param config Configuration to render.
/// @return The same text the member returns.
/// @note Spec `metrics/metric-provider`, requirement "Provider configuration
///       serialization"; diagnostic only (project mandate 9).
[[nodiscard]] std::string to_string(const MetricProviderConfig& config);

/// @brief Write the provider configuration as JSON (cold path).
///
/// @param j Output JSON object with the interval in nanoseconds, the complete
///        worker thread spec, and every capacity.
/// @param config Configuration to serialize.
/// @note Spec `metrics/metric-provider`, requirement "Provider configuration
///       serialization"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const MetricProviderConfig& config);
/// @brief Parse a provider configuration from JSON without throwing.
///
/// Reads numeric fields with type checks, prefers the canonical `worker_thread`
/// object over the legacy `core_mask`, clamps `collect_interval_ns` to a
/// representable `std::chrono::nanoseconds` range, and leaves unspecified
/// capacities unchanged.
///
/// @param j JSON object to read.
/// @param out Destination, fields updated independently.
/// @note Spec `metrics/snapshot-format`, requirement "MetricProviderConfig
///       deserialization uses safe nlohmann accessors".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, MetricProviderConfig& out) noexcept;

} // namespace nexigen::metrics

/// @brief `std::formatter` for `MetricProviderConfig`, rendering its diagnostics.
///
/// @note Project mandate 11; formatting is cold path only.
template <>
struct std::formatter<nexigen::metrics::MetricProviderConfig> : std::formatter<std::string_view> {
    /// @brief Format the configuration through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param value Configuration to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::metrics::MetricProviderConfig& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
