// nexigen::canopen — one CiA 306 object dictionary entry.
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nexigen/canopen/eds_access.hpp>
#include <nexigen/canopen/eds_numeric.hpp>
#include <nexigen/canopen/eds_object_type.hpp>

namespace nexigen::canopen {

/// @brief One object dictionary entry.
struct EdsObject {
    std::uint16_t index{0};                     ///< Object index.
    std::uint8_t subindex{0};                   ///< Subindex.
    std::string name{};                         ///< Parameter name.
    EdsObjectType type{EdsObjectType::Unknown}; ///< Object type.
    std::uint16_t data_type{0};                 ///< CiA 301 data type code.
    EdsAccess access{EdsAccess::Unknown};       ///< Access type.
    bool pdo_mappable{false};                   ///< `PDOMapping=1`.
    bool has_default{false};                    ///< Numeric default value present.
    EdsNumeric default_value{};                 ///< Numeric default.
    bool has_cob_id{false};                     ///< `COB-ID` key present.
    EdsNumeric cob_id{};                        ///< COB-ID value.
    std::uint16_t sub_number{0};                ///< Declared subobject count.
    std::uint16_t compact_sub_obj{0};           ///< `CompactSubObj` count.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line object summary.
    [[nodiscard]] std::string to_string() const {
        return std::format(
            "EdsObject(index=0x{:04x}, sub={}, name={}, type={}, data_type=0x{:04x})", index,
            subindex, name, nexigen::canopen::to_string(type), data_type);
    }
};

/// @brief Free helper mirroring `EdsObject::to_string`.
[[nodiscard]] inline std::string to_string(const EdsObject& object) {
    return object.to_string();
}

} // namespace nexigen::canopen
