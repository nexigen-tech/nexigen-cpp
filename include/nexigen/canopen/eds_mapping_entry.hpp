// nexigen::canopen — one PDO mapping entry.
#pragma once

#include <cstdint>
#include <format>
#include <string>

namespace nexigen::canopen {

/// @brief One PDO mapping entry.
struct EdsMappingEntry {
    std::uint16_t index{0};   ///< Mapped object index.
    std::uint8_t subindex{0}; ///< Mapped subindex.
    std::uint8_t bits{0};     ///< Bit length; zero terminates the mapping.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line mapping summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("EdsMappingEntry(index=0x{:04x}, sub={}, bits={})", index, subindex,
                           bits);
    }
};

/// @brief Free helper mirroring `EdsMappingEntry::to_string`.
[[nodiscard]] inline std::string to_string(const EdsMappingEntry& entry) {
    return entry.to_string();
}

} // namespace nexigen::canopen
