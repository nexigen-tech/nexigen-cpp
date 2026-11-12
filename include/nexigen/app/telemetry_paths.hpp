// nexigen::app — resolved output locations (spec app/telemetry-layout).
#pragma once
#include <string>
#include <nlohmann/json.hpp>
namespace nexigen::app {

/// @brief Resolved output locations of the three JSONL telemetry streams.
///
/// Each member holds an absolute path, or an empty string meaning "the helper
/// does not configure that sink". The resolver fills them independently:
/// an explicit settings path wins over the instance-directory fallback.
///
/// @note Spec `app/telemetry-layout`, requirement "Sink paths resolve from
///       settings, then instance identity".
/// @note Rendering, JSON and formatting follow project mandates 9, 10 and 11.
struct TelemetryPaths {
    std::string metrics{}; ///< Metrics JSONL path ("" = unconfigured).
    std::string traces{};  ///< Traces JSONL path ("" = unconfigured).
    std::string chrono{};  ///< Chrono JSONL path ("" = unconfigured).

    /// @brief Render the resolved paths as a cold-path diagnostic string.
    ///
    /// @return Text naming all three stream locations.
    /// @note Spec `app/telemetry-layout`, requirement "Identity and resolved
    ///       paths are serializable"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};
/// @brief Free diagnostic helper mirroring `TelemetryPaths::to_string()`.
///
/// @param paths Resolved paths to render.
/// @return The same text the member returns.
/// @note Diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const TelemetryPaths& paths) {
    return paths.to_string();
}
/// @brief Write the resolved paths as JSON (cold path).
///
/// @param j Output JSON object with the metrics, traces and chrono paths.
/// @param paths Resolved paths to serialize.
/// @note Spec `app/telemetry-layout`, requirement "Identity and resolved paths
///       are serializable"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const TelemetryPaths& paths);
/// @brief Read resolved paths from JSON (cold path).
///
/// @param j Input JSON; a non-object or wrong-typed field leaves the matching
///        member unchanged.
/// @param paths Destination, never throwing under `JSON_NOEXCEPTION=1`.
/// @note Project mandate 10: hostile JSON must not throw or abort.
void from_json(const nlohmann::json& j, TelemetryPaths& paths) noexcept;
} // namespace nexigen::app
