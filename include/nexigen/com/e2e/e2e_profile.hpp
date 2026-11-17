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
///
/// @param value Profile to render.
/// @return The canonical enumerator name; allocation-free and `constexpr`.
/// @note Spec `com/e2e`, requirement "E2E profile vocabulary"; mandate 9
///       (universal `to_string`).
[[nodiscard]] constexpr std::string_view to_string(E2eProfile value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical E2E profile name.
///
/// @param name Candidate canonical name, as produced by `to_string`.
/// @param out Receives the parsed profile; left UNCHANGED when parsing fails.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Spec `com/e2e`, requirement "E2E profile vocabulary"; mandate 9.
[[nodiscard]] constexpr bool from_string(std::string_view name, E2eProfile& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for an E2E profile.
///
/// @param j Output JSON value, replaced by the canonical name `string`.
/// @param value Profile to serialize.
/// @note Spec `com/e2e`, requirement "E2E profile vocabulary"; mandate 10
///       (universal JSON). Allocates, so cold path only.
inline void to_json(nlohmann::json& j, const E2eProfile value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for an E2E profile (non-throwing).
///
/// @param j Input JSON value; a non-string or unknown name leaves `out`
///        unchanged.
/// @param out Receives the parsed profile on success.
/// @note Spec `com/e2e`, requirement "E2E profile vocabulary"; mandate 10.
inline void from_json(const nlohmann::json& j, E2eProfile& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::com

namespace nexigen::core {

/// @brief `E2eProfile` vocabulary table.
template <> struct EnumTraits<nexigen::com::E2eProfile> {
    /// @brief One value/name pair of the vocabulary table.
    struct Entry {
        nexigen::com::E2eProfile value; ///< Enumerator.
        std::string_view name;          ///< Canonical name.
    };
    /// @brief Canonical table; exactly one entry per enumerator.
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
    /// @brief Write the canonical profile name into the format context.
    /// @tparam Ctx `std::format` context type.
    /// @param value Profile to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Spec `com/e2e`, requirement "E2E profile vocabulary".
    template <class Ctx> auto format(const nexigen::com::E2eProfile value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::com::to_string(value), ctx);
    }
};
