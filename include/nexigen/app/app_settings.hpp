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

/// Merged metrics settings (spec app/settings "Metrics settings map every
/// provider field"). Loader-produced: to_json only, no from_json.
struct MetricsSettings {
    metrics::MetricProviderConfig options{};           ///< Provider configuration.
    metrics::JsonFileMetricSinkConfig sink{};           ///< File-sink path, append flag.
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<logging::SettingsDiagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const MetricsSettings& settings) {
    return settings.to_string();
}

/// Merged tracing settings (spec app/settings "Tracing settings map every
/// provider field"). Loader-produced: to_json only, no from_json.
struct TracingSettings {
    tracing::TracerProviderConfig options{};           ///< Provider configuration.
    tracing::JsonFileTraceSinkConfig sink{};            ///< File-sink path, rotation, rate limits.
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<logging::SettingsDiagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const TracingSettings& settings) {
    return settings.to_string();
}

/// The four explicit source pairs consumed by load_app_settings.
struct SettingsPaths {
    logging::SettingsPaths logging{};
    logging::SettingsPaths metrics{};
    logging::SettingsPaths tracing{};
    chrono::SettingsPaths chrono{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const SettingsPaths& paths) {
    return paths.to_string();
}

/// One merged value consumed by Infrastructure::initialize.
struct Settings {
    logging::Settings logging{};
    MetricsSettings metrics{};
    TracingSettings tracing{};
    chrono::Settings chrono{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Settings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (bodies in src/app/app_settings.cpp)
void to_json(nlohmann::json& j, const MetricsSettings& settings);
void to_json(nlohmann::json& j, const TracingSettings& settings);
void to_json(nlohmann::json& j, const SettingsPaths& paths);
void to_json(nlohmann::json& j, const Settings& settings);
// No from_json for loader-produced settings or SettingsPaths: they are
// reconstructed by the loaders (pinned at compile time in the test TU).

/// Load one subsystem. `out` is always populated best effort. Missing files
/// are not errors; an unreadable file is SystemError; invalid values are
/// InvalidArgument diagnostics.
[[nodiscard]] core::Status load_metrics_settings(const logging::SettingsPaths& paths,
                                                 MetricsSettings& out) noexcept;
[[nodiscard]] core::Status load_tracing_settings(const logging::SettingsPaths& paths,
                                                 TracingSettings& out) noexcept;

/// Load logging + metrics + tracing. Reports SystemError if any supplied file
/// could not be read, otherwise InvalidArgument if any diagnostic exists,
/// otherwise OK. Every subsystem stays populated best effort.
[[nodiscard]] core::Status load_app_settings(const SettingsPaths& paths,
                                             Settings& out) noexcept;

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
struct std::formatter<nexigen::app::SettingsPaths>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::SettingsPaths& paths, Ctx& ctx) const {
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
