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

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::chrono {

/// @brief Base clock domains available on every Linux system.
///
/// Closed `uint8_t` enum of the kernel clock bases the ecosystem may name.
/// `monotonic_raw` is the canonical hot-path stamp domain; `boottime` is
/// reserved and not sampled in this version; `realtime` is the disciplined
/// wall clock and may step. Values round-trip through `to_string`/`from_string`,
/// `to_json`/`from_json`, and the shared generic `std::formatter`.
///
/// @note Spec `chrono/clock-id`, requirement "Base clock-domain identifiers";
///       project mandates 9–11.
enum class ClockId : std::uint8_t {
    monotonic_raw, ///< Raw monotonic clock, not frequency-disciplined (hot-path canonical).
    boottime,      ///< Monotonic clock including suspend time.
    realtime,      ///< Wall clock disciplined by NTP/PTP (may step).
};

} // namespace nexigen::chrono

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table wiring `ClockId` into the shared enum engine.
///
/// Supplies the canonical wire names consumed by `core::enum_name` /
/// `core::enum_value` and therefore by `to_string`, `from_string`, and the JSON
/// thunks. The table is `static constexpr` storage: no allocation, no lock.
///
/// @note Spec `chrono/clock-id`, requirement "Base clock-domain identifiers";
///       project mandates 9 and 10.
template <> struct EnumTraits<nexigen::chrono::ClockId> {
    /// @brief One `ClockId` to canonical-name row.
    struct Entry {
        nexigen::chrono::ClockId value; ///< Enumerator described by this row.
        std::string_view name;          ///< Canonical wire spelling.
    };
    /// @brief Append-only rows in enumerator order.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::chrono::ClockId::monotonic_raw, "monotonic_raw"},
        {nexigen::chrono::ClockId::boottime, "boottime"},
        {nexigen::chrono::ClockId::realtime, "realtime"},
    }};
};

} // namespace nexigen::core

namespace nexigen::chrono {

/// @brief Canonical name of a clock domain.
///
/// Allocation-free and `constexpr`; values outside the vocabulary table render
/// the engine fallback `"Unknown"`.
///
/// @param id Clock domain to name.
/// @return Static-storage view of the canonical spelling.
/// @note Spec `chrono/clock-id`, requirement "Base clock-domain identifiers";
///       project mandate 9.
[[nodiscard]] constexpr std::string_view to_string(ClockId id) noexcept {
    return core::enum_name(id);
}

/// @brief Parse a canonical clock-domain name.
///
/// Exact, case-sensitive match. On any other spelling (including `"Unknown"`,
/// which is a rendering and not an enumerator) the function returns `false`
/// and leaves `out` unchanged.
///
/// @param name Candidate canonical spelling.
/// @param out Destination enumerator, untouched on a miss.
/// @return `true` when `name` matched a row and `out` was assigned.
/// @note Spec `chrono/clock-id`, requirement "Base clock-domain identifiers";
///       project mandate 9.
[[nodiscard]] constexpr bool from_string(std::string_view name, ClockId& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for `ClockId` (cold path only).
///
/// The value is exactly `to_string(id)`; initialization and diagnostics only,
/// never a producer or delivery path.
///
/// @param j Destination JSON value, replaced by the canonical name string.
/// @param id Clock domain to serialize.
/// @note Spec `chrono/clock-id`, requirement "Base clock-domain identifiers";
///       project mandate 10.
inline void to_json(nlohmann::json& j, const ClockId id) {
    core::mandate::enum_to_json(j, id);
}

/// @brief ADL JSON reader for `ClockId`, strictly non-throwing.
///
/// Under `JSON_NOEXCEPTION=1` non-string values yield `nullptr` from `get_ptr`
/// and unknown names miss the table, so `out` stays unchanged in both cases.
///
/// @param j Source JSON value.
/// @param out Destination enumerator, untouched unless a known name parses.
/// @note Spec `chrono/clock-id`, requirement "Base clock-domain identifiers";
///       project mandate 10.
inline void from_json(const nlohmann::json& j, ClockId& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

/// @brief Compile-time clock-domain declaration for a component.
///
/// A component specializes this variable template to report the clock domain
/// it stamps in; reading it is a constant expression with no object instance,
/// no allocation, no lock, and no clock read. The primary template reports the
/// canonical hot-path domain `monotonic_raw`, so an undeclared component is
/// treated as monotonic-raw.
///
/// @tparam T Component type declaring its domain.
/// @note Spec `chrono/clock-id`, requirement "Compile-time domain declaration".
template <class T> inline constexpr ClockId clock_domain = ClockId::monotonic_raw;

} // namespace nexigen::chrono
