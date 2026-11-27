// nexigen::canopen — CiA 306 EDS access vocabulary.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canopen {

/// @brief EDS access type.
enum class EdsAccess : std::uint8_t {
    Unknown,         ///< Missing or unrecognised.
    ReadOnly,        ///< `ro`.
    WriteOnly,       ///< `wo`.
    ReadWrite,       ///< `rw`.
    ReadWriteInput,  ///< `rwr`.
    ReadWriteOutput, ///< `rww`.
    Constant,        ///< `const`.
};

} // namespace nexigen::canopen

namespace nexigen::core {

/// @brief Vocabulary table for `EdsAccess`.
template <> struct EnumTraits<nexigen::canopen::EdsAccess> {
    /// @brief One value/name row.
    struct Entry {
        nexigen::canopen::EdsAccess value; ///< Enumerator.
        std::string_view name;             ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 7> entries{{
        {nexigen::canopen::EdsAccess::Unknown, "Unknown"},
        {nexigen::canopen::EdsAccess::ReadOnly, "ReadOnly"},
        {nexigen::canopen::EdsAccess::WriteOnly, "WriteOnly"},
        {nexigen::canopen::EdsAccess::ReadWrite, "ReadWrite"},
        {nexigen::canopen::EdsAccess::ReadWriteInput, "ReadWriteInput"},
        {nexigen::canopen::EdsAccess::ReadWriteOutput, "ReadWriteOutput"},
        {nexigen::canopen::EdsAccess::Constant, "Constant"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canopen {

/// @brief Canonical name of an EDS access type.
[[nodiscard]] constexpr std::string_view to_string(EdsAccess value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse an EDS access type.
[[nodiscard]] constexpr bool from_string(std::string_view name, EdsAccess& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write an EDS access type as JSON.
inline void to_json(nlohmann::json& j, const EdsAccess value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read an EDS access type without throwing.
inline void from_json(const nlohmann::json& j, EdsAccess& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

/// @brief Parse an EDS access keyword.
/// @param text Keyword, case-insensitive.
/// @return Access type.
[[nodiscard]] EdsAccess parse_eds_access(std::string_view text) noexcept;

} // namespace nexigen::canopen
