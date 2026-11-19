// nexigen::chrono — time-domain role vocabulary.
//
// A domain class describes what a row is: a host system clock, a host cycle
// counter, or a hardware clock owned by an interface module. The class is
// deliberately separate from `TimeDomain`, which names the scale itself.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::chrono {

/// @brief Role of a time domain in the published table.
///
/// @note Spec `chrono/time-domain`, requirement "Two-level time-domain
///       vocabulary".
enum class TimeDomainClass : std::uint8_t {
    SystemClock,  ///< Host `CLOCK_*` system scale.
    CycleCounter, ///< Host cycle counter (TSC on x86).
    DeviceClock,  ///< Hardware timer, PHC, or register device.
};

} // namespace nexigen::chrono

namespace nexigen::core {

/// @brief Vocabulary table wiring `TimeDomainClass` into the shared engine.
template <> struct EnumTraits<nexigen::chrono::TimeDomainClass> {
    /// @brief One class-to-name row.
    struct Entry {
        nexigen::chrono::TimeDomainClass value; ///< Enumerator described here.
        std::string_view name;                  ///< Canonical wire spelling.
    };
    /// @brief Append-only rows in enumerator order.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::chrono::TimeDomainClass::SystemClock, "system_clock"},
        {nexigen::chrono::TimeDomainClass::CycleCounter, "cycle_counter"},
        {nexigen::chrono::TimeDomainClass::DeviceClock, "device_clock"},
    }};
};

} // namespace nexigen::core

namespace nexigen::chrono {

/// @brief Canonical name of a domain class.
/// @param value Class to name.
/// @return Static-storage name, or `"Unknown"` for an out-of-table value.
[[nodiscard]] constexpr std::string_view to_string(TimeDomainClass value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical domain-class name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` when the name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, TimeDomainClass& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for `TimeDomainClass`.
/// @param j Destination JSON value.
/// @param value Class to serialize.
inline void to_json(nlohmann::json& j, const TimeDomainClass value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for `TimeDomainClass`, non-throwing.
/// @param j Source JSON value.
/// @param out Destination, untouched on a miss.
inline void from_json(const nlohmann::json& j, TimeDomainClass& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::chrono

/// @brief `std::formatter` for `TimeDomainClass`.
template <>
struct std::formatter<nexigen::chrono::TimeDomainClass> : std::formatter<std::string_view> {
    /// @brief Format the class through its canonical name.
    /// @tparam Ctx Format context type.
    /// @param value Class to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::chrono::TimeDomainClass value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::chrono::to_string(value), ctx);
    }
};
