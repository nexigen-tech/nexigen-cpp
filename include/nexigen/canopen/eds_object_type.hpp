// nexigen::canopen — CiA 306 object type.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canopen {

/// @brief CiA 306 object type.
enum class EdsObjectType : std::uint8_t {
    Var,     ///< Single variable.
    Array,   ///< Array object.
    Record,  ///< Record object.
    Unknown, ///< Missing/unrecognised.
};

} // namespace nexigen::canopen

namespace nexigen::core {

/// @brief Vocabulary table for `EdsObjectType`.
template <> struct EnumTraits<nexigen::canopen::EdsObjectType> {
    /// @brief One value/name row.
    struct Entry {
        nexigen::canopen::EdsObjectType value; ///< Enumerator.
        std::string_view name;                 ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::canopen::EdsObjectType::Var, "Var"},
        {nexigen::canopen::EdsObjectType::Array, "Array"},
        {nexigen::canopen::EdsObjectType::Record, "Record"},
        {nexigen::canopen::EdsObjectType::Unknown, "Unknown"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canopen {

/// @brief Canonical name of an EDS object type.
[[nodiscard]] constexpr std::string_view to_string(EdsObjectType value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse an EDS object type.
[[nodiscard]] constexpr bool from_string(std::string_view name, EdsObjectType& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write an EDS object type as JSON.
inline void to_json(nlohmann::json& j, const EdsObjectType value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read an EDS object type without throwing.
inline void from_json(const nlohmann::json& j, EdsObjectType& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canopen
