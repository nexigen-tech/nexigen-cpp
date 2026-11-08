// nexigen::app — the four explicit source pairs (spec app/settings).
#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <nexigen/core/settings_paths.hpp>
namespace nexigen::app {
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
void to_json(nlohmann::json& j, const AppSettingsPaths& paths);
} // namespace nexigen::app
