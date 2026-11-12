// nexigen::app — merged app settings (spec app/settings).
// AppSettingsPaths was split into app_settings_paths.hpp.
#pragma once

#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <nexigen/app/app_settings_paths.hpp>
#include <nexigen/chrono/settings.hpp>
#include <nexigen/core/settings_source.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/logging/settings.hpp>
#include <nexigen/metrics/metrics_settings.hpp>
#include <nexigen/tracing/tracing_settings.hpp>

namespace nexigen::app {

/// @brief One merged value consumed by `Infrastructure::initialize`.
///
/// Aggregates the four subsystem settings with the application name and the
/// signal-watcher thread spec. The loader builds it on the cold path; no tick,
/// producer, snapshot, or sink path reads it. An empty `app_name` means that
/// no application identity is imposed.
///
/// @note Spec `app/settings`, requirement "App settings aggregate every
///       subsystem".
/// @note Spec `app/settings`, requirement "Application name is app-level".
/// @note Rendering, JSON and formatting follow project mandates 9, 10 and 11.
struct Settings {
    std::string app_name{};           ///< Application name for telemetry (supplied from the CLI).
    logging::Settings logging{};      ///< Merged logging settings.
    metrics::Settings metrics{};      ///< Merged metrics settings.
    tracing::Settings tracing{};      ///< Merged tracing settings.
    chrono::Settings chrono{};        ///< Merged chrono settings.
    core::ThreadSpec signal_thread{}; ///< signal-watcher spec (empty = inherited)

    /// @brief Stable named construction path that keeps `Settings` an aggregate.
    ///
    /// Legacy positional initialization stays valid because `signal_thread` is
    /// appended last; this factory only assigns the application name.
    ///
    /// @param name Application name for telemetry.
    /// @return A default-constructed aggregate carrying `name` as `app_name`.
    /// @note Spec `app/settings`, requirement "Application name is app-level".
    [[nodiscard]] static Settings create(std::string name) {
        Settings settings;
        settings.app_name = std::move(name);
        return settings;
    }

    /// @brief Render the aggregate as a cold-path diagnostic string.
    ///
    /// @return Text naming the application and every subsystem's rendering.
    /// @note Spec `app/settings`, requirement "App settings are serializable";
    ///       diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};
/// @brief Free diagnostic helper mirroring `Settings::to_string()`.
///
/// @param settings Aggregate to render.
/// @return The same text the member returns.
/// @note Spec `app/settings`, requirement "App settings are serializable";
///       diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const Settings& settings) {
    return settings.to_string();
}

/// @brief Write the aggregate as JSON (cold path).
///
/// @param j Output JSON object covering `app_name`, `signal_thread` and the
///        four subsystem members.
/// @param settings Aggregate to serialize.
/// @note Spec `app/settings`, requirement "App settings are serializable";
///       JSON only (project mandate 10).
/// @note Deliberately no `from_json`: the aggregate is reconstructed by the
///       four subsystem loaders, so the overload is pinned absent.
void to_json(nlohmann::json& j, const Settings& settings);

/// @brief Load logging, metrics, tracing and chrono through their loaders.
///
/// Delegates to the four subsystem loaders and always populates `out`
/// best-effort, so a partially bad file still yields usable settings.
///
/// @param paths The four explicit global/local source pairs.
/// @param out Destination aggregate, usable after the call.
/// @return `SystemError` when a supplied file could not be read; otherwise
///         `InvalidArgument` when any diagnostic exists; otherwise OK.
/// @note Spec `app/settings`, requirement "App settings aggregate every
///       subsystem".
/// @note Cold path only: called before workers start, never from a tick,
///       producer, or worker hot path.
[[nodiscard]] core::Status load_app_settings(const AppSettingsPaths& paths, Settings& out) noexcept;

} // namespace nexigen::app
