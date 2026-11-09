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

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::chrono {

/// Severity of the clock service's own worker diagnostics. Module-local so
/// chrono does not depend on the logging module; the application bridges its
/// logger to the worker's log hook.
enum class ClockLogLevel : std::uint8_t {
    info,    ///< normal operational milestone
    warning, ///< recoverable problem (pin failure, snapshot delivery failure)
    error,   ///< hard failure
};

} // namespace nexigen::chrono

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::chrono::ClockLogLevel> {
    struct Entry {
        nexigen::chrono::ClockLogLevel value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::chrono::ClockLogLevel::info, "info"},
        {nexigen::chrono::ClockLogLevel::warning, "warning"},
        {nexigen::chrono::ClockLogLevel::error, "error"},
    }};
};

} // namespace nexigen::core

namespace nexigen::chrono {

/// Canonical name of a log level; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(ClockLogLevel level) noexcept {
    return core::enum_name(level);
}

/// Parse a canonical ClockLogLevel name; false leaves the out-parameter
/// UNCHANGED on any other spelling.
[[nodiscard]] constexpr bool from_string(std::string_view name, ClockLogLevel& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the value is exactly to_string(level).
inline void to_json(nlohmann::json& j, const ClockLogLevel level) {
    core::mandate::enum_to_json(j, level);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, ClockLogLevel& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::chrono
