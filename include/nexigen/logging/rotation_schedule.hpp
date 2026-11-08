// nexigen::logging — scheduled directory rotation schedule selector (design
// D11, spec logging/file-logger "Hitch-free scheduled directory rotation").
//
// Public vocabulary enum consumed by RotationConfig and FileLoggerConfig; the
// rotation machine (detail/rotation.hpp) consumes the same enumerators.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::logging {

/// Schedule selector (None, Hourly, Daily, or Custom epoch-anchored windows).
enum class RotationSchedule : std::uint8_t { None, Hourly, Daily, Custom };

/// Compile-time lowercase name — the spelling used by from_string/JSON.
[[nodiscard]] constexpr std::string_view to_string(RotationSchedule kind) noexcept {
    switch (kind) {
        case RotationSchedule::None:   return "none";
        case RotationSchedule::Hourly: return "hourly";
        case RotationSchedule::Daily:  return "daily";
        case RotationSchedule::Custom: return "custom";
        default:                       return "Unknown";
    }
}

/// Parse a canonical lowercase schedule name; false leaves `out` unchanged.
[[nodiscard]] constexpr bool from_string(std::string_view name, RotationSchedule& out) noexcept {
    if (name == "none") {
        out = RotationSchedule::None;
        return true;
    }
    if (name == "hourly") {
        out = RotationSchedule::Hourly;
        return true;
    }
    if (name == "daily") {
        out = RotationSchedule::Daily;
        return true;
    }
    if (name == "custom") {
        out = RotationSchedule::Custom;
        return true;
    }
    return false;
}

/// ADL JSON writer (cold path only): the JSON value is exactly to_string(kind).
inline void to_json(nlohmann::json& j, const RotationSchedule kind) {
    j = std::string(to_string(kind));
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, RotationSchedule& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

} // namespace nexigen::logging
