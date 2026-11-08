// nexigen::runner — AppState enum.
#pragma once
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::runner {

enum class AppState : std::uint8_t {
    Running,
    Restarting,
    Exited,
    Stopped,
    Failed,
    Unknown,
};

[[nodiscard]] constexpr std::string_view to_string(AppState value) noexcept {
    switch (value) {
        case AppState::Running:    return "running";
        case AppState::Restarting: return "restarting";
        case AppState::Exited:     return "exited";
        case AppState::Stopped:    return "stopped";
        case AppState::Failed:     return "failed";
        case AppState::Unknown:    return "unknown";
        default:                   return "unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name, AppState& out) noexcept {
    if (name == "running") {
        out = AppState::Running;
        return true;
    }
    if (name == "restarting") {
        out = AppState::Restarting;
        return true;
    }
    if (name == "exited") {
        out = AppState::Exited;
        return true;
    }
    if (name == "stopped") {
        out = AppState::Stopped;
        return true;
    }
    if (name == "failed") {
        out = AppState::Failed;
        return true;
    }
    if (name == "unknown") {
        out = AppState::Unknown;
        return true;
    }
    return false;
}

void to_json(nlohmann::json& j, AppState value);
void from_json(const nlohmann::json& j, AppState& value) noexcept;

} // namespace nexigen::runner
