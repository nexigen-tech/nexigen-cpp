// nexigen::app — application settings (change
// refactor-settings-into-subsystem-modules; spec app/settings).
//
// COLD PATH ONLY. The app layer now AGGREGATES the four subsystem settings:
// each subsystem (logging, metrics, tracing, chrono) owns its own `Settings`
// type and loader, and `load_app_settings` delegates to the four loaders and
// reports the worst status. The shared source/diagnostic vocabulary lives in
// `nexigen::core`.
#pragma once

#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/chrono_settings.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/settings_vocabulary.hpp>
#include <nexigen/logging/logging_settings.hpp>
#include <nexigen/metrics/metrics_settings.hpp>
#include <nexigen/tracing/tracing_settings.hpp>

namespace nexigen::app {

/// The four explicit source pairs consumed by load_app_settings.
struct AppSettingsPaths {
    core::SettingsPaths logging{};
    core::SettingsPaths metrics{};
    core::SettingsPaths tracing{};
    core::SettingsPaths chrono{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const AppSettingsPaths& paths) {
    return paths.to_string();
}

/// One merged value consumed by Infrastructure::initialize.
struct Settings {
    logging::Settings logging{};
    metrics::Settings metrics{};
    tracing::Settings tracing{};
    chrono::Settings chrono{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Settings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (bodies in src/app/app_settings.cpp)
void to_json(nlohmann::json& j, const AppSettingsPaths& paths);
void to_json(nlohmann::json& j, const Settings& settings);
// No from_json for the aggregate or AppSettingsPaths: they are reconstructed
// by the loaders (pinned at compile time in the test TU).

/// Load logging + metrics + tracing + chrono by delegating to the four
/// subsystem loaders. Reports SystemError if any supplied file could not be
/// read, otherwise InvalidArgument if any diagnostic exists, otherwise OK.
/// Every subsystem stays populated best effort.
[[nodiscard]] core::Status load_app_settings(const AppSettingsPaths& paths,
                                             Settings& out) noexcept;

} // namespace nexigen::app

template <>
struct std::formatter<nexigen::app::AppSettingsPaths>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::AppSettingsPaths& paths, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(paths.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::app::Settings>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::Settings& settings, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(settings.to_string(), ctx);
    }
};
