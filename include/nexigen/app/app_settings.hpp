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

#include <nexigen/chrono/chrono_settings.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/logging_settings.hpp>
#include <nexigen/metrics/json_file_metric_sink.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/tracing/json_file_trace_sink.hpp>
#include <nexigen/tracing/tracer_provider.hpp>

namespace nexigen::app {

/// Reuse the logging pipeline's explicit path and diagnostic vocabulary.
using Settings_paths = logging::Settings_paths;
using Settings_diagnostic = logging::Settings_diagnostic;

/// Merged metrics settings (spec app/settings "Metrics settings map every
/// provider field"). Loader-produced: to_json only, no from_json.
struct MetricsSettings {
    metrics::MetricProvider_config options{};           ///< Provider configuration.
    metrics::JsonFileMetricSink_config sink{};           ///< File-sink path, append flag.
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<Settings_diagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const MetricsSettings& settings) {
    return settings.to_string();
}

/// Merged tracing settings (spec app/settings "Tracing settings map every
/// provider field"). Loader-produced: to_json only, no from_json.
struct TracingSettings {
    tracing::TracerProvider_config options{};           ///< Provider configuration.
    tracing::JsonFileTraceSink_config sink{};            ///< File-sink path, rotation, rate limits.
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<Settings_diagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const TracingSettings& settings) {
    return settings.to_string();
}

/// The four explicit source pairs consumed by load_app_settings.
struct AppSettingsPaths {
    logging::Settings_paths logging{};
    logging::Settings_paths metrics{};
    logging::Settings_paths tracing{};
    chrono::Chrono_settings_paths chrono{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const AppSettingsPaths& paths) {
    return paths.to_string();
}

/// One merged value consumed by Infrastructure::initialize.
struct AppSettings {
    logging::Logging_settings logging{};
    MetricsSettings metrics{};
    TracingSettings tracing{};
    chrono::Chrono_settings chrono{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const AppSettings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (bodies in src/app/app_settings.cpp)
void to_json(nlohmann::json& j, const MetricsSettings& settings);
void to_json(nlohmann::json& j, const TracingSettings& settings);
void to_json(nlohmann::json& j, const AppSettingsPaths& paths);
void to_json(nlohmann::json& j, const AppSettings& settings);
// No from_json for loader-produced settings or AppSettingsPaths: they are
// reconstructed by the loaders (pinned at compile time in the test TU).

/// Load one subsystem. `out` is always populated best effort. Missing files
/// are not errors; an unreadable file is SystemError; invalid values are
/// InvalidArgument diagnostics.
[[nodiscard]] core::Status load_metrics_settings(const Settings_paths& paths,
                                                 MetricsSettings& out) noexcept;
[[nodiscard]] core::Status load_tracing_settings(const Settings_paths& paths,
                                                 TracingSettings& out) noexcept;

/// Load logging + metrics + tracing. Reports SystemError if any supplied file
/// could not be read, otherwise InvalidArgument if any diagnostic exists,
/// otherwise OK. Every subsystem stays populated best effort.
[[nodiscard]] core::Status load_app_settings(const AppSettingsPaths& paths,
                                             AppSettings& out) noexcept;

} // namespace nexigen::app

template <>
struct std::formatter<nexigen::app::MetricsSettings>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::MetricsSettings& settings, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(settings.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::app::TracingSettings>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::TracingSettings& settings, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(settings.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::app::AppSettingsPaths>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::AppSettingsPaths& paths, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(paths.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::app::AppSettings>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::AppSettings& settings, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(settings.to_string(), ctx);
    }
};
