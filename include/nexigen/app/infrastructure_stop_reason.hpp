// nexigen::app — why the infrastructure stopped (change
// rework-app-as-infrastructure; spec app/application).
//
// Full enum contract: constexpr to_string/from_string, non-throwing ADL
// to_json/from_json under JSON_NOEXCEPTION=1, and std::formatter. The values
// are stable and append-only.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::app {

/// Why the infrastructure run loop stopped.
enum class InfrastructureStopReason : std::uint8_t {
    None,      ///< No stop requested yet.
    Requested, ///< Host code requested a stop (explicit call or false tick).
    Sigint,    ///< SIGINT received.
    Sigterm,   ///< SIGTERM received.
};

[[nodiscard]] constexpr std::string_view to_string(InfrastructureStopReason reason) noexcept {
    switch (reason) {
        case InfrastructureStopReason::None:      return "none";
        case InfrastructureStopReason::Requested: return "requested";
        case InfrastructureStopReason::Sigint:    return "sigint";
        case InfrastructureStopReason::Sigterm:   return "sigterm";
        default:                                  return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         InfrastructureStopReason& out) noexcept {
    if (name == "none") {
        out = InfrastructureStopReason::None;
        return true;
    }
    if (name == "requested") {
        out = InfrastructureStopReason::Requested;
        return true;
    }
    if (name == "sigint") {
        out = InfrastructureStopReason::Sigint;
        return true;
    }
    if (name == "sigterm") {
        out = InfrastructureStopReason::Sigterm;
        return true;
    }
    return false;
}

inline void to_json(nlohmann::json& j, const InfrastructureStopReason reason) {
    j = std::string(to_string(reason));
}

inline void from_json(const nlohmann::json& j, InfrastructureStopReason& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

} // namespace nexigen::app
