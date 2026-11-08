// nexigen::app — merged app settings (spec app/settings).
// AppSettingsPaths was split into app_settings_paths.hpp.
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/app/app_settings_paths.hpp>
#include <nexigen/chrono/settings.hpp>
#include <nexigen/core/settings_source.hpp>
#include <nexigen/logging/settings.hpp>
#include <nexigen/metrics/metrics_settings.hpp>
#include <nexigen/tracing/tracing_settings.hpp>

namespace nexigen::app {

/// One merged value consumed by Infrastructure::initialize.
struct Settings {
    std::string app_name{}; ///< Application name for telemetry (supplied from the CLI).
    logging::Settings logging{};
    metrics::Settings metrics{};
    tracing::Settings tracing{};
    chrono::Settings chrono{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Settings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (body in src/app/app_settings.cpp)
void to_json(nlohmann::json& j, const Settings& settings);
// No from_json for the aggregate: it is reconstructed by the loaders.

/// Load logging + metrics + tracing + chrono by delegating to the four
/// subsystem loaders. Reports SystemError if any supplied file could not be
/// read, otherwise InvalidArgument if any diagnostic exists, otherwise OK.
[[nodiscard]] core::Status load_app_settings(const AppSettingsPaths& paths, Settings& out) noexcept;

} // namespace nexigen::app
