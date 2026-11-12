// nexigen::app — the four explicit source pairs (spec app/settings).
#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <nexigen/core/settings_paths.hpp>
namespace nexigen::app {

/// @brief The four explicit global/local source pairs of one settings load.
///
/// Each member carries the pair consumed by one subsystem loader. An empty
/// pair means that source (global or local) is disabled for that subsystem; the
/// command-line mapping fills it from the two supplied directories.
///
/// @note Spec `app/settings`, requirement "App settings aggregate every
///       subsystem".
/// @note Rendering, JSON and formatting follow project mandates 9, 10 and 11.
struct AppSettingsPaths {
    core::SettingsPaths logging{}; ///< Logging global/local file pair.
    core::SettingsPaths metrics{}; ///< Metrics global/local file pair.
    core::SettingsPaths tracing{}; ///< Tracing global/local file pair.
    core::SettingsPaths chrono{};  ///< Chrono global/local file pair.

    /// @brief Render the four pairs as a cold-path diagnostic string.
    ///
    /// @return Text naming every configured global and local path.
    /// @note Spec `app/settings`, requirement "App settings are serializable";
    ///       diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};
/// @brief Free diagnostic helper mirroring `AppSettingsPaths::to_string()`.
///
/// @param paths Path pairs to render.
/// @return The same text the member returns.
/// @note Spec `app/settings`, requirement "App settings are serializable";
///       diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const AppSettingsPaths& paths) {
    return paths.to_string();
}
/// @brief Write the four path pairs as JSON (cold path).
///
/// @param j Output JSON object with one member per subsystem pair.
/// @param paths Path pairs to serialize.
/// @note Spec `app/settings`, requirement "App settings are serializable";
///       JSON only (project mandate 10).
void to_json(nlohmann::json& j, const AppSettingsPaths& paths);
} // namespace nexigen::app
