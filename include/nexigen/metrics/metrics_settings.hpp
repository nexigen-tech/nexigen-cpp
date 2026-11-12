// nexigen::metrics — global/local metrics settings loader (change
// refactor-settings-into-subsystem-modules; spec metrics/metrics-settings).
//
// COLD PATH ONLY. `load_metrics_settings` runs once before real-time
// execution: it reads an explicitly supplied global JSON file, then an
// explicitly supplied local JSON file, then applies environment overrides,
// merging field by field into a MetricProviderConfig plus a file-sink config.
// Every problem becomes a `core::SettingsDiagnostic`; a bad value leaves the
// previous/default value in place so the caller can still initialize a
// provider. The loader never touches a producer or collection hot path.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/settings_diagnostic.hpp>
#include <nexigen/core/settings_paths.hpp>
#include <nexigen/core/settings_source.hpp>
#include <nexigen/metrics/json_file_metric_sink.hpp>
#include <nexigen/metrics/metric_provider.hpp>

namespace nexigen::metrics {

/// @brief Merged result of one settings load.
///
/// `global_loaded`/`local_loaded` report file presence, not success;
/// `diagnostics` carries every non-fatal problem encountered while merging.
/// The loader always populates this value best-effort, so a partially bad file
/// still yields a usable provider configuration.
///
/// @note Spec `metrics/metrics-settings`, requirement "Merge precedence matches
///       the other subsystems".
/// @note Spec `metrics/metrics-settings`, requirement "Metrics settings carry a
///       sink member".
struct Settings {
    MetricProviderConfig options{};  ///< Provider configuration.
    JsonFileMetricSinkConfig sink{}; ///< File-sink path and append flag.
    bool global_loaded{false};       ///< Global JSON file existed and was parsed.
    bool local_loaded{false};        ///< Local JSON file existed and was parsed.
    std::vector<core::SettingsDiagnostic> diagnostics{}; ///< Non-fatal problems.

    /// @brief Render the merged settings as a cold-path diagnostic string.
    ///
    /// @return Text covering provider fields, the sink, loaded flags, and
    ///         diagnostics.
    /// @note Spec `metrics/metrics-settings`, requirement "Metrics settings are
    ///       serializable"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};
/// @brief Free diagnostic helper mirroring `Settings::to_string()`.
///
/// @param settings Settings to render.
/// @return The same text the member returns.
/// @note Spec `metrics/metrics-settings`, requirement "Metrics settings are
///       serializable"; diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const Settings& settings) {
    return settings.to_string();
}

/// @brief Write the merged settings as JSON (cold path).
///
/// @param j Output JSON object with provider fields, sink fields, loaded flags,
///        and diagnostics.
/// @param settings Settings to serialize.
/// @note Spec `metrics/metrics-settings`, requirement "Metrics settings are
///       serializable"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const Settings& settings);
// Deliberately no from_json(Settings&): the loader reconstructs Settings. The
// absence is a pinned contract; see the metrics-settings spec.

/// @brief Read, merge, and validate metrics settings from explicit files.
///
/// The global file is parsed first, then the local file, then the documented
/// environment overrides are applied field by field. `out` is always populated
/// best-effort, so the caller can initialize a provider even after a failure.
///
/// @param paths Explicit global/local source paths; empty means "not supplied".
/// @param out Destination, always usable after the call.
/// @return `Ok` when no diagnostic was recorded; `InvalidArgument` when a
///         value, type, or JSON problem was recorded; `SystemError` when a
///         supplied file exists but cannot be read. Missing files are not
///         errors.
/// @note Spec `metrics/metrics-settings`, requirement "Settings sources are
///       explicit".
/// @note Spec `metrics/metrics-settings`, requirement "Invalid input degrades
///       to best effort".
/// @note Cold path only: never called from a collection or sink hot path.
[[nodiscard]] core::Status load_metrics_settings(const core::SettingsPaths& paths,
                                                 Settings& out) noexcept;

} // namespace nexigen::metrics
