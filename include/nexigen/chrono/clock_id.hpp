// nexigen::chrono — base clock-domain identifiers and the compile-time domain
// declaration (change add-chrono-clock-service; spec chrono/clock-id).
//
// Clock_id enumerates the base clock domains present on every Linux system:
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
enum class Clock_id : std::uint8_t {
    monotonic_raw, ///< raw monotonic clock, not frequency-disciplined (hot-path canonical)
    boottime,      ///< monotonic clock including suspend time
    realtime,      ///< wall clock, disciplined by NTP/PTP (may step)
};

/// Canonical name of a clock domain; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(Clock_id id) noexcept {
    switch (id) {
        case Clock_id::monotonic_raw: return "monotonic_raw";
        case Clock_id::boottime:      return "boottime";
        case Clock_id::realtime:      return "realtime";
        default:                      return "Unknown";
    }
}

/// Parse a canonical Clock_id name; false leaves the out-parameter UNCHANGED
/// on any other spelling (including "Unknown" — a rendering, not an enumerator).
[[nodiscard]] constexpr bool from_string(std::string_view name, Clock_id& out) noexcept {
    if (name == "monotonic_raw") { out = Clock_id::monotonic_raw; return true; }
    if (name == "boottime")      { out = Clock_id::boottime;      return true; }
    if (name == "realtime")      { out = Clock_id::realtime;      return true; }
    return false;
}

/// ADL JSON writer (cold path only): the value is exactly to_string(id).
inline void to_json(nlohmann::json& j, const Clock_id id) {
    j = std::string(to_string(id));
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1: non-string
/// values yield nullptr from get_ptr; unknown names leave `out` unchanged.
inline void from_json(const nlohmann::json& j, Clock_id& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// Compile-time clock-domain declaration. A component specializes this
/// constant to report the clock it stamps in; the default is the canonical
/// hot-path domain monotonic_raw.
template <class T>
inline constexpr Clock_id clock_domain = Clock_id::monotonic_raw;

} // namespace nexigen::chrono

/// Clock_id formats as its canonical name (mandate 11): parse and fill/align
/// handled by the string_view formatter; the context is templated because
/// libstdc++ 15 evaluates formattability against a substituted context.
template <>
struct std::formatter<nexigen::chrono::Clock_id> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::chrono::Clock_id id, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::chrono::to_string(id), ctx);
    }
};
