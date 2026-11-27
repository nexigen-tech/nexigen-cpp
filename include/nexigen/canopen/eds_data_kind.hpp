// nexigen::canopen — CiA 301 data-kind vocabulary.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canopen {

/// @brief Kind of a CiA 301 data type.
enum class EdsDataKind : std::uint8_t {
    Boolean,  ///< BOOLEAN.
    Integer,  ///< Signed integer.
    Unsigned, ///< Unsigned integer.
    Real,     ///< REAL32/REAL64.
    String,   ///< VISIBLE/OCTET/UNICODE string.
    Time,     ///< TIME_OF_DAY/TIME_DIFFERENCE.
    Domain,   ///< DOMAIN.
    Unknown,  ///< Missing or unrecognised.
};

} // namespace nexigen::canopen

namespace nexigen::core {

/// @brief Vocabulary table for `EdsDataKind`.
template <> struct EnumTraits<nexigen::canopen::EdsDataKind> {
    /// @brief One value/name row.
    struct Entry {
        nexigen::canopen::EdsDataKind value; ///< Enumerator.
        std::string_view name;               ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 8> entries{{
        {nexigen::canopen::EdsDataKind::Boolean, "Boolean"},
        {nexigen::canopen::EdsDataKind::Integer, "Integer"},
        {nexigen::canopen::EdsDataKind::Unsigned, "Unsigned"},
        {nexigen::canopen::EdsDataKind::Real, "Real"},
        {nexigen::canopen::EdsDataKind::String, "String"},
        {nexigen::canopen::EdsDataKind::Time, "Time"},
        {nexigen::canopen::EdsDataKind::Domain, "Domain"},
        {nexigen::canopen::EdsDataKind::Unknown, "Unknown"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canopen {

/// @brief Canonical name of a data kind.
[[nodiscard]] constexpr std::string_view to_string(EdsDataKind value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse a data kind.
[[nodiscard]] constexpr bool from_string(std::string_view name, EdsDataKind& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write a data kind as JSON.
inline void to_json(nlohmann::json& j, const EdsDataKind value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read a data kind without throwing.
inline void from_json(const nlohmann::json& j, EdsDataKind& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canopen
