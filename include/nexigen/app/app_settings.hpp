// nexigen::app — application settings (change add-app-class; spec
// app/settings).
//
// COLD PATH ONLY. Each subsystem reads its own explicit global/local JSON
// pair with the same precedence and best-effort diagnostics as
// logging::load_logging_settings: defaults → global → local → environment.
// `load_app_settings` aggregates all three and reports the worst status.
#pragma once

#include <format>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/logging_settings.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/tracing/tracer_provider.hpp>

namespace nexigen::app {

/// Reuse the logging pipeline's explicit path and diagnostic vocabulary.
using Settings_paths = logging::Settings_paths;
using Settings_diagnostic = logging::Settings_diagnostic;

/// Merged metrics settings (spec app/settings "Metrics settings map every
/// provider field"). Loader-produced: to_json only, no from_json.
struct Metrics_settings {
    metrics::MetricProvider_config options{};
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<Settings_diagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Metrics_settings& settings) {
    return settings.to_string();
}

/// Merged tracing settings (spec app/settings "Tracing settings map every
/// provider field"). Loader-produced: to_json only, no from_json.
struct Tracing_settings {
    tracing::TracerProvider_config options{};
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<Settings_diagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Tracing_settings& settings) {
    return settings.to_string();
}

/// The three explicit source pairs consumed by load_app_settings.
struct App_settings_paths {
    logging::Settings_paths logging{};
    logging::Settings_paths metrics{};
    logging::Settings_paths tracing{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const App_settings_paths& paths) {
    return paths.to_string();
}

/// One merged value consumed by Infrastructure::initialize.
struct App_settings {
    logging::Logging_settings logging{};
    Metrics_settings metrics{};
    Tracing_settings tracing{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const App_settings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (bodies in src/app/app_settings.cpp)
void to_json(nlohmann::json& j, const Metrics_settings& settings);
void to_json(nlohmann::json& j, const Tracing_settings& settings);
void to_json(nlohmann::json& j, const App_settings_paths& paths);
void to_json(nlohmann::json& j, const App_settings& settings);
// No from_json for loader-produced settings or App_settings_paths: they are
// reconstructed by the loaders (pinned at compile time in the test TU).

/// Load one subsystem. `out` is always populated best effort. Missing files
/// are not errors; an unreadable file is SystemError; invalid values are
/// InvalidArgument diagnostics.
[[nodiscard]] core::Status load_metrics_settings(const Settings_paths& paths,
                                                 Metrics_settings& out) noexcept;
[[nodiscard]] core::Status load_tracing_settings(const Settings_paths& paths,
                                                 Tracing_settings& out) noexcept;

/// Load logging + metrics + tracing. Reports SystemError if any supplied file
/// could not be read, otherwise InvalidArgument if any diagnostic exists,
/// otherwise OK. Every subsystem stays populated best effort.
[[nodiscard]] core::Status load_app_settings(const App_settings_paths& paths,
                                             App_settings& out) noexcept;

} // namespace nexigen::app

template <>
struct std::formatter<nexigen::app::Metrics_settings>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::Metrics_settings& settings, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(settings.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::app::Tracing_settings>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::Tracing_settings& settings, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(settings.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::app::App_settings_paths>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::App_settings_paths& paths, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(paths.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::app::App_settings>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::App_settings& settings, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(settings.to_string(), ctx);
    }
};
