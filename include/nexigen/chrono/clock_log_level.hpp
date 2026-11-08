// nexigen::chrono — clock service worker diagnostics severity (change
// add-chrono-clock-service; spec chrono/chrono-provider).
//
// Module-local severity so chrono does not depend on the logging module; the
// application bridges its logger to the worker's log hook. Serialization trio
// (mandates 9–11): constexpr to_string/from_string and non-throwing ADL
// to_json/from_json under JSON_NOEXCEPTION=1. Like ClockId, ClockLogLevel
// formats through the shared generic std::formatter (core/serialization
// mandate) — no per-type formatter specialization.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::chrono {

/// Severity of the clock service's own worker diagnostics. Module-local so
/// chrono does not depend on the logging module; the application bridges its
/// logger to the worker's log hook.
enum class ClockLogLevel : std::uint8_t {
    info,    ///< normal operational milestone
    warning, ///< recoverable problem (pin failure, snapshot delivery failure)
    error,   ///< hard failure
};

/// Canonical name of a log level; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(ClockLogLevel level) noexcept {
    switch (level) {
        case ClockLogLevel::info:    return "info";
        case ClockLogLevel::warning: return "warning";
        case ClockLogLevel::error:   return "error";
        default:                     return "Unknown";
    }
}

/// Parse a canonical ClockLogLevel name; false leaves the out-parameter
/// UNCHANGED on any other spelling.
[[nodiscard]] constexpr bool from_string(std::string_view name, ClockLogLevel& out) noexcept {
    if (name == "info") {
        out = ClockLogLevel::info;
        return true;
    }
    if (name == "warning") {
        out = ClockLogLevel::warning;
        return true;
    }
    if (name == "error") {
        out = ClockLogLevel::error;
        return true;
    }
    return false;
}

/// ADL JSON writer (cold path only): the value is exactly to_string(level).
inline void to_json(nlohmann::json& j, const ClockLogLevel level) {
    j = std::string(to_string(level));
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, ClockLogLevel& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

} // namespace nexigen::chrono
