// nexigen::canopen — CiA 301 data-type descriptor.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/canopen/eds_data_kind.hpp>

namespace nexigen::canopen {

/// @brief One CiA 301 data type descriptor.
struct EdsDataTypeInfo {
    std::uint16_t code{0};                  ///< EDS data type code.
    std::string_view name{};                ///< Canonical name.
    std::uint8_t bits{0};                   ///< Bit width; zero for variable-length strings.
    EdsDataKind kind{EdsDataKind::Unknown}; ///< Interpretation.
    bool signed_value{false};               ///< Signed integer.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line data-type summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("EdsDataTypeInfo(code=0x{:04x}, name={}, bits={}, kind={})", code, name,
                           bits, nexigen::canopen::to_string(kind));
    }
};

/// @brief Free helper mirroring `EdsDataTypeInfo::to_string`.
[[nodiscard]] inline std::string to_string(const EdsDataTypeInfo& info) {
    return info.to_string();
}

/// @brief Resolve a CiA 301 data type code.
/// @param code EDS data type code.
/// @return Descriptor; an unknown code returns the `Unknown` entry.
[[nodiscard]] const EdsDataTypeInfo& eds_data_type(std::uint16_t code) noexcept;

} // namespace nexigen::canopen
