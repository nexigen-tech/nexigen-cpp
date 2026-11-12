// nexigen::tracing — global/local tracing settings loader (change
// refactor-settings-into-subsystem-modules; spec tracing/tracing-settings).
//
// COLD PATH ONLY. `load_tracing_settings` runs once before real-time
// execution: it reads an explicitly supplied global JSON file, then an
// explicitly supplied local JSON file, then applies environment overrides,
// merging field by field into a TracerProviderConfig plus a file-sink config.
// Every problem becomes a `core::SettingsDiagnostic`; a bad value leaves the
// previous/default value in place so the caller can still initialize a
// provider. The loader never touches a span or sink hot path.
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
#include <nexigen/tracing/json_file_trace_sink.hpp>
#include <nexigen/tracing/tracer_provider.hpp>

namespace nexigen::tracing {

/// @brief Merged result of one settings load.
///
/// Carries the provider configuration, the file-sink configuration, the
/// per-source presence flags, and every non-fatal diagnostic. The type is a
/// best-effort value: even a failed load leaves usable defaults in place.
///
/// @note Spec `tracing/tracing-settings`, requirements "Tracing settings carry
///       a sink member" and "Invalid input degrades to best effort".
struct Settings {
    TracerProviderConfig options{};                      ///< Provider configuration.
    JsonFileTraceSinkConfig sink{};                      ///< File-sink path, rotation, rate limits.
    bool global_loaded{false};                           ///< Global file was present and parsed.
    bool local_loaded{false};                            ///< Local file was present and parsed.
    std::vector<core::SettingsDiagnostic> diagnostics{}; ///< Every non-fatal problem.

    /// @brief Render the merged settings as a cold-path diagnostic string.
    ///
    /// @return Text naming the provider options, the full sink configuration,
    ///         both loaded flags, and the diagnostics.
    /// @note Spec `tracing/tracing-settings`, requirement "Tracing settings are
    ///       serializable"; project mandate 9 (`to_string`).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `Settings::to_string()`.
///
/// @param settings Settings to render.
/// @return The same text the member returns.
/// @note Project mandate 9 (`to_string`); cold path only.
[[nodiscard]] inline std::string to_string(const Settings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (body in src/tracing/tracing_settings.cpp)

/// @brief Serialize the merged settings as JSON (cold path).
///
/// Includes the full sink configuration in addition to provider fields, the
/// loaded flags, and the diagnostics. There is deliberately no
/// `from_json(Settings&)` because the loader reconstructs the object.
///
/// @param j Destination JSON value, overwritten with the merged settings.
/// @param settings Settings to serialize.
/// @note Spec `tracing/tracing-settings`, requirement "Tracing settings are
///       serializable"; project mandate 10 (JSON).
void to_json(nlohmann::json& j, const Settings& settings);
// Deliberately no from_json(Settings&): reconstructed by the loader.

/// @brief Read, merge, and validate tracing settings from explicit paths.
///
/// Reads the optional global JSON file, then the optional local file, then
/// applies the documented environment overrides, merging field by field in the
/// precedence defaults → global → local → environment. A missing or empty path
/// is not an error; every malformed value becomes a diagnostic while the last
/// valid/default value is retained, so `out` is always populated best-effort.
///
/// @param paths Explicit global and local source paths supplied by the caller.
/// @param out Destination settings, always populated; never reset on failure.
/// @return Success when no diagnostic was recorded, `InvalidArgument` when a
///         value/type/JSON problem was recorded, or `SystemError` when a
///         supplied file exists but cannot be read.
/// @note Spec `tracing/tracing-settings`, requirements "Settings sources are
///       explicit", "Merge precedence matches the other subsystems", and
///       "Invalid input degrades to best effort"; cold path only.
[[nodiscard]] core::Status load_tracing_settings(const core::SettingsPaths& paths,
                                                 Settings& out) noexcept;

} // namespace nexigen::tracing
