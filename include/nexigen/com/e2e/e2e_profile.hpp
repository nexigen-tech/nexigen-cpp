// nexigen::com — end-to-end protection profile vocabulary (change
// add-e2e-protection).
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::com {

/// @brief Selected E2E profile.
///
/// `None` disables protection. `P04`/`P07` use CRC-32 with the AUTOSAR
/// polynomial; `P05`/`P06` use CRC-16/CCITT. Counter widths differ per profile
/// (see `e2e_counter_mask`).
///
/// @note Spec `com/e2e`, requirement "E2E profile vocabulary".
enum class E2eProfile : std::uint8_t {
    None, ///< No protection.
    P04,  ///< CRC-32, 16-bit counter.
    P05,  ///< CRC-16, 8-bit counter.
    P06,  ///< CRC-16, 16-bit counter.
    P07,  ///< CRC-32, 32-bit counter.
};

/// @brief Canonical name of an E2E profile.
[[nodiscard]] constexpr std::string_view to_string(E2eProfile value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical E2E profile name.
[[nodiscard]] constexpr bool from_string(std::string_view name, E2eProfile& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for an E2E profile.
inline void to_json(nlohmann::json& j, const E2eProfile value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for an E2E profile (non-throwing).
inline void from_json(const nlohmann::json& j, E2eProfile& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::com

namespace nexigen::core {

/// @brief `E2eProfile` vocabulary table.
template <> struct EnumTraits<nexigen::com::E2eProfile> {
    struct Entry {
        nexigen::com::E2eProfile value; ///< Enumerator.
        std::string_view name;          ///< Canonical name.
    };
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::com::E2eProfile::None, "None"},
        {nexigen::com::E2eProfile::P04, "P04"},
        {nexigen::com::E2eProfile::P05, "P05"},
        {nexigen::com::E2eProfile::P06, "P06"},
        {nexigen::com::E2eProfile::P07, "P07"},
    }};
};

} // namespace nexigen::core

/// @brief `std::formatter` for an E2E profile (project mandate 11).
template <> struct std::formatter<nexigen::com::E2eProfile> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::com::E2eProfile value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::com::to_string(value), ctx);
    }
};
