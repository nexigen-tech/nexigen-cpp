// nexigen::chrono — base clock-domain identifiers and the compile-time domain
// declaration (change add-chrono-clock-service; spec chrono/clock-id).
//
// ClockId enumerates the base clock domains present on every Linux system:
// monotonic_raw (the raw, non-frequency-disciplined monotonic clock — the
// canonical hot-path stamp domain), boottime (monotonic including suspend
// time), and realtime (the wall clock disciplined by NTP/PTP, which may step).
//
// Serialization trio (mandates 9–11): constexpr to_string/from_string,
// non-throwing ADL to_json/from_json under JSON_NOEXCEPTION=1, and a
// std::formatter specialization (templated format context, like ErrorCode).
//
// `clock_domain<T>` is the compile-time domain declaration: a variable
// template defaulting to monotonic_raw, specialized per component. Reading it
// is a constant expression — no object instance, no allocation, no lock, no
// clock read (spec chrono/clock-id "Compile-time domain declaration").
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::chrono {

/// Base clock domains available on every Linux system.
enum class ClockId : std::uint8_t {
    monotonic_raw, ///< raw monotonic clock, not frequency-disciplined (hot-path canonical)
    boottime,      ///< monotonic clock including suspend time
    realtime,      ///< wall clock, disciplined by NTP/PTP (may step)
};

/// Canonical name of a clock domain; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(ClockId id) noexcept {
    switch (id) {
        case ClockId::monotonic_raw: return "monotonic_raw";
        case ClockId::boottime:      return "boottime";
        case ClockId::realtime:      return "realtime";
        default:                      return "Unknown";
    }
}

/// Parse a canonical ClockId name; false leaves the out-parameter UNCHANGED
/// on any other spelling (including "Unknown" — a rendering, not an enumerator).
[[nodiscard]] constexpr bool from_string(std::string_view name, ClockId& out) noexcept {
    if (name == "monotonic_raw") { out = ClockId::monotonic_raw; return true; }
    if (name == "boottime")      { out = ClockId::boottime;      return true; }
    if (name == "realtime")      { out = ClockId::realtime;      return true; }
    return false;
}

/// ADL JSON writer (cold path only): the value is exactly to_string(id).
inline void to_json(nlohmann::json& j, const ClockId id) {
    j = std::string(to_string(id));
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1: non-string
/// values yield nullptr from get_ptr; unknown names leave `out` unchanged.
inline void from_json(const nlohmann::json& j, ClockId& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// Compile-time clock-domain declaration. A component specializes this
/// constant to report the clock it stamps in; the default is the canonical
/// hot-path domain monotonic_raw.
template <class T>
inline constexpr ClockId clock_domain = ClockId::monotonic_raw;

} // namespace nexigen::chrono

/// ClockId formats as its canonical name (mandate 11): parse and fill/align
/// handled by the string_view formatter; the context is templated because
/// libstdc++ 15 evaluates formattability against a substituted context.

