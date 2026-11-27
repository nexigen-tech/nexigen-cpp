// nexigen::canopen — CiA 306 numeric value.
#pragma once

#include <cstdint>
#include <format>
#include <string>

namespace nexigen::canopen {

/// @brief Numeric EDS value, optionally relative to the node id.
struct EdsNumeric {
    bool valid{false};            ///< Whether the text parsed as a number.
    bool node_relative{false};    ///< Value contains `$NODEID`.
    std::uint32_t value{0};       ///< Literal part.
    std::uint32_t node_offset{0}; ///< Added to the node id when relative.

    /// @brief Resolve against one node id.
    /// @param node_id Node id (1..127).
    /// @return Resolved 32-bit value.
    [[nodiscard]] std::uint32_t resolve(std::uint8_t node_id) const noexcept {
        return value + (node_relative ? static_cast<std::uint32_t>(node_id) : 0U) + node_offset;
    }

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line numeric summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("EdsNumeric(valid={}, relative={}, value=0x{:x}, offset=0x{:x})", valid,
                           node_relative, value, node_offset);
    }
};

/// @brief Free helper mirroring `EdsNumeric::to_string`.
[[nodiscard]] inline std::string to_string(const EdsNumeric& value) {
    return value.to_string();
}

} // namespace nexigen::canopen
