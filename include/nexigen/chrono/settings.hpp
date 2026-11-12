// nexigen::chrono — global/local chrono settings loader (change
// add-chrono-clock-service; spec chrono/chrono-settings).
//
// COLD PATH ONLY. `load_chrono_settings` runs once before real-time
// execution: it reads an explicitly supplied global JSON file, then an
// explicitly supplied local JSON file, then applies environment overrides,
// merging field by field into a ProviderConfig. Every problem becomes a
// `core::SettingsDiagnostic`; a bad value leaves the previous/default value in
// place so the caller can still initialize a provider.
//
// The shared `SettingsSource`/`SettingsPaths`/`SettingsDiagnostic` vocabulary
// lives in `nexigen::core`, so chrono no longer duplicates it (and no longer
// needs to depend on the logging module to avoid the duplication).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/json_file_sink_config.hpp>
#include <nexigen/chrono/provider_config.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/settings_diagnostic.hpp>
#include <nexigen/core/settings_paths.hpp>
#include <nexigen/core/settings_source.hpp>

namespace nexigen::chrono {

/// @brief Merged result of one chrono settings load.
///
/// Carries the provider configuration, the file-sink configuration, per-source
/// presence flags, and every non-fatal diagnostic. The loader always populates
/// this value best-effort, so a caller may construct a provider even when the
/// returned `Status` reports a problem. `global_loaded`/`local_loaded` report
/// file presence, not parse success.
///
/// @note Spec `chrono/chrono-settings`, requirement "Invalid input degrades to
///       best effort"; requirement "Settings diagnostics are serializable".
struct Settings {
    ProviderConfig options{};  ///< Merged provider configuration.
    JsonFileSinkConfig sink{}; ///< Merged file-sink path and append flag.
    bool global_loaded{false}; ///< Whether the supplied global file was present.
    bool local_loaded{false};  ///< Whether the supplied local file was present.
    std::vector<core::SettingsDiagnostic> diagnostics{}; ///< Every non-fatal finding.

    /// @brief Render the merged settings as a diagnostic string.
    ///
    /// The body lives in `src/chrono/settings.cpp`; allocates.
    ///
    /// @return The rendered text naming provider fields, sink fields, loaded
    ///         flags, and diagnostics.
    /// @note Spec `chrono/chrono-settings`, requirement "Settings diagnostics
    ///       are serializable"; project mandate 9.
    [[nodiscard]] std::string to_string() const;
};
/// @brief Free-helper mirror of `Settings::to_string`.
///
/// @param settings Merged settings to render.
/// @return The same text as `settings.to_string()`.
/// @note Spec `chrono/chrono-settings`, requirement "Settings diagnostics are
///       serializable"; project mandate 9.
[[nodiscard]] inline std::string to_string(const Settings& settings) {
    return settings.to_string();
}

/// @brief Serialize merged settings as JSON (cold path).
///
/// Includes provider fields, the full sink configuration, loaded flags, and
/// diagnostics. The body lives in `src/chrono/settings.cpp`. There is
/// deliberately no `from_json(Settings&)`: the loader is the only
/// reconstruction path, so a partially read document can never masquerade as a
/// merged result.
///
/// @param j Destination JSON value, replaced by the serialized settings.
/// @param settings Merged settings to serialize.
/// @note Spec `chrono/chrono-settings`, requirement "Settings diagnostics are
///       serializable"; requirement "Sink configuration is part of chrono
///       settings serialization"; project mandate 10.
void to_json(nlohmann::json& j, const Settings& settings);

/// @brief Read, merge, and validate chrono settings from explicit paths.
///
/// Precedence is built-in defaults, then the global file, then the local file,
/// then the documented environment overrides; absent fields inherit from the
/// lower-priority source. The loader never consults implicit directories, and
/// `out` is always populated best-effort. Missing or empty paths are not
/// failures.
///
/// @param paths Explicit global/local file paths; empty disables a source.
/// @param out Destination merged settings, populated even on failure.
/// @return `core::ErrorCode::Ok` when no diagnostic was recorded,
///         `core::ErrorCode::InvalidArgument` when a value, type, or JSON
///         problem was recorded, or `core::ErrorCode::SystemError` when a
///         supplied file exists but cannot be read.
/// @note Spec `chrono/chrono-settings`, requirement "Settings sources are
///       explicit"; requirement "Merge precedence is deterministic";
///       requirement "Invalid input degrades to best effort"; requirement
///       "Settings loading is cold path only".
[[nodiscard]] core::Status load_chrono_settings(const core::SettingsPaths& paths,
                                                Settings& out) noexcept;

/// @brief Map merged settings into a provider construction config.
///
/// Transfers the interval, the complete worker thread spec, and the provider
/// kind. Sink configuration is intentionally not transferred: sink construction
/// is a separate policy concern (`app/telemetry-layout`).
///
/// @param settings Merged settings to read.
/// @param out Destination provider configuration, replaced by `options`.
/// @return Always success today; the `core::Status` shape keeps the mapping
///         extensible without changing the call sites.
/// @note Spec `chrono/chrono-settings`, requirement "Merged settings apply to
///       provider configuration".
[[nodiscard]] core::Status apply_to(const Settings& settings, ProviderConfig& out) noexcept;

} // namespace nexigen::chrono
