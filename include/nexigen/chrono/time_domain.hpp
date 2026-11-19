// nexigen::chrono — well-known time-domain identifiers and declarations.
//
// `TimeDomain::MonotonicRaw` (CLOCK_MONOTONIC_RAW) is the hub scale. System
// scales and the host cycle counter have fixed ids; declared hardware domains
// use the `16..47` range and are named by their declaration.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>
#include <nexigen/chrono/time_domain_class.hpp>

namespace nexigen::chrono {

/// @brief Well-known time-domain identifiers.
///
/// Device domains start at `device_domain_base`; their concrete id is the base
/// plus the declaration index.
///
/// @note Spec `chrono/time-domain`, requirement "Well-known and device domain
///       identifiers".
enum class TimeDomain : std::uint16_t {
    MonotonicRaw = 0, ///< `CLOCK_MONOTONIC_RAW`, the hub.
    Monotonic = 1,    ///< `CLOCK_MONOTONIC`.
    Tai = 2,          ///< `CLOCK_TAI`.
    Realtime = 3,     ///< `CLOCK_REALTIME`.
    HostCounter = 4,  ///< Host cycle counter (TSC on x86).
};

/// @brief Id of the `CLOCK_MONOTONIC_RAW` hub row.
inline constexpr std::uint16_t k_monotonic_raw_domain_id = 0U;
/// @brief Id of the `CLOCK_MONOTONIC` row.
inline constexpr std::uint16_t k_monotonic_domain_id = 1U;
/// @brief Id of the `CLOCK_TAI` row.
inline constexpr std::uint16_t k_tai_domain_id = 2U;
/// @brief Id of the `CLOCK_REALTIME` row.
inline constexpr std::uint16_t k_realtime_domain_id = 3U;
/// @brief Id of the host cycle-counter row.
inline constexpr std::uint16_t k_host_counter_domain_id = 4U;
/// @brief First id of a declared hardware domain.
inline constexpr std::uint16_t device_domain_base = 16U;
/// @brief Last id of a declared hardware domain.
inline constexpr std::uint16_t device_domain_last = 47U;

/// @brief Domain id of a declared hardware domain.
/// @param index Zero-based declaration index.
/// @return `device_domain_base + index`, or `device_domain_last` when out of range.
[[nodiscard]] constexpr std::uint16_t device_domain_id(std::uint16_t index) noexcept {
    const std::uint32_t id = static_cast<std::uint32_t>(device_domain_base) + index;
    return id <= device_domain_last ? static_cast<std::uint16_t>(id) : device_domain_last;
}

/// @brief Compile-time declaration of the domain a component stamps in.
///
/// Defaults to the hub scale. A component may specialize this variable
/// template without runtime state.
///
/// @tparam T Component type.
/// @note Spec `chrono/time-domain`, requirement "Compile-time domain
///       declaration".
template <class T> inline constexpr TimeDomain time_domain = TimeDomain::MonotonicRaw;

/// @brief Class of a well-known domain.
/// @param domain Domain to classify.
/// @return `SystemClock` for host clocks, `CycleCounter` for the host counter.
[[nodiscard]] constexpr TimeDomainClass domain_class(TimeDomain domain) noexcept {
    return domain == TimeDomain::HostCounter ? TimeDomainClass::CycleCounter
                                             : TimeDomainClass::SystemClock;
}

} // namespace nexigen::chrono

namespace nexigen::core {

/// @brief Vocabulary table wiring `TimeDomain` into the shared engine.
template <> struct EnumTraits<nexigen::chrono::TimeDomain> {
    /// @brief One domain-to-name row.
    struct Entry {
        nexigen::chrono::TimeDomain value; ///< Enumerator described here.
        std::string_view name;             ///< Canonical wire spelling.
    };
    /// @brief Append-only rows in enumerator order.
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::chrono::TimeDomain::MonotonicRaw, "monotonic_raw"},
        {nexigen::chrono::TimeDomain::Monotonic, "monotonic"},
        {nexigen::chrono::TimeDomain::Tai, "tai"},
        {nexigen::chrono::TimeDomain::Realtime, "realtime"},
        {nexigen::chrono::TimeDomain::HostCounter, "host_counter"},
    }};
};

} // namespace nexigen::core

namespace nexigen::chrono {

/// @brief Canonical name of a well-known time domain.
/// @param domain Domain to name.
/// @return Static-storage name, or `"Unknown"` for an out-of-table value.
[[nodiscard]] constexpr std::string_view to_string(TimeDomain domain) noexcept {
    return core::enum_name(domain);
}

/// @brief Parse a well-known time-domain name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` when the name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, TimeDomain& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for `TimeDomain`.
/// @param j Destination JSON value.
/// @param domain Domain to serialize.
inline void to_json(nlohmann::json& j, const TimeDomain domain) {
    core::mandate::enum_to_json(j, domain);
}

/// @brief ADL JSON reader for `TimeDomain`, non-throwing.
/// @param j Source JSON value.
/// @param domain Destination, untouched on a miss.
inline void from_json(const nlohmann::json& j, TimeDomain& domain) noexcept {
    core::mandate::enum_from_json(j, domain);
}

} // namespace nexigen::chrono

/// @brief `std::formatter` for `TimeDomain`.
template <> struct std::formatter<nexigen::chrono::TimeDomain> : std::formatter<std::string_view> {
    /// @brief Format the domain through its canonical name.
    /// @tparam Ctx Format context type.
    /// @param value Domain to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::chrono::TimeDomain value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::chrono::to_string(value), ctx);
    }
};
