// nexigen::runner — SuiteState enum.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace nexigen::runner {

enum class SuiteState : std::uint8_t {
    Running,
    Degraded,
    Failed,
    Stopped,
    Unknown,
};

[[nodiscard]] constexpr std::string_view to_string(SuiteState value) noexcept {
    switch (value) {
        case SuiteState::Running:  return "running";
        case SuiteState::Degraded: return "degraded";
        case SuiteState::Failed:   return "failed";
        case SuiteState::Stopped:  return "stopped";
        case SuiteState::Unknown:  return "unknown";
        default:                   return "unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name, SuiteState& out) noexcept {
    if (name == "running") {
        out = SuiteState::Running;
        return true;
    }
    if (name == "degraded") {
        out = SuiteState::Degraded;
        return true;
    }
    if (name == "failed") {
        out = SuiteState::Failed;
        return true;
    }
    if (name == "stopped") {
        out = SuiteState::Stopped;
        return true;
    }
    if (name == "unknown") {
        out = SuiteState::Unknown;
        return true;
    }
    return false;
}

} // namespace nexigen::runner
