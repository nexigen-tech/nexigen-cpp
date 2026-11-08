// nexigen::chrono — global/local chrono settings loader (change
// add-chrono-clock-service; spec chrono/chrono-settings).
//
// COLD PATH ONLY. `load_chrono_settings` runs once before real-time
// execution: it reads an explicitly supplied global JSON file, then an
// explicitly supplied local JSON file, then applies environment overrides,
// merging field by field into a ChronoProviderConfig. Every problem becomes a
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

#include <nexigen/chrono/chrono_provider.hpp>
#include <nexigen/chrono/json_file_chrono_sink.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/settings_diagnostic.hpp>
#include <nexigen/core/settings_paths.hpp>
#include <nexigen/core/settings_source.hpp>

namespace nexigen::chrono {

/// Merged result of one load. `global_loaded`/`local_loaded` report file
/// presence, not success; `diagnostics` carries every non-fatal problem.
struct Settings {
    ChronoProviderConfig options{};  ///< Provider configuration.
    JsonFileChronoSinkConfig sink{}; ///< File-sink path and append flag.
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<core::SettingsDiagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Settings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (body in src/chrono/settings.cpp)
void to_json(nlohmann::json& j, const Settings& settings);
// Deliberately no from_json(Settings&): reconstructed by the loader.

/// Read, merge and validate settings. `out` is always populated best-effort.
/// Returns Ok when no diagnostic was recorded, InvalidArgument when a value /
/// type / JSON problem was recorded, and SystemError when a supplied file
/// exists but cannot be read. Missing files are not errors.
[[nodiscard]] core::Status load_chrono_settings(const core::SettingsPaths& paths,
                                                Settings& out) noexcept;

/// Map merged settings into a provider construction config.
[[nodiscard]] core::Status apply_to(const Settings& settings, ChronoProviderConfig& out) noexcept;

} // namespace nexigen::chrono
