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

/// Merged result of one load. `global_loaded`/`local_loaded` report file
/// presence, not success; `diagnostics` carries every non-fatal problem.
struct Settings {
    TracerProviderConfig options{}; ///< Provider configuration.
    JsonFileTraceSinkConfig sink{}; ///< File-sink path, rotation, rate limits.
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<core::SettingsDiagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Settings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (body in src/tracing/tracing_settings.cpp)
void to_json(nlohmann::json& j, const Settings& settings);
// Deliberately no from_json(Settings&): reconstructed by the loader.

/// Read, merge and validate settings. `out` is always populated best-effort.
/// Returns Ok when no diagnostic was recorded, InvalidArgument when a value /
/// type / JSON problem was recorded, and SystemError when a supplied file
/// exists but cannot be read. Missing files are not errors.
[[nodiscard]] core::Status load_tracing_settings(const core::SettingsPaths& paths,
                                                 Settings& out) noexcept;

} // namespace nexigen::tracing
