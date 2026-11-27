// nexigen::canopen — default PDO mapping from an EDS.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string>

#include <nexigen/canopen/eds_mapping_entry.hpp>

namespace nexigen::canopen {

/// @brief Default PDO mapping from one `[1600..17FF]`/`[1A00..1BFF]` object.
struct EdsPdoMapping {
    std::uint16_t mapping_index{0};           ///< Mapping object index.
    std::uint8_t pdo{0};                      ///< 1..4.
    bool transmit{false};                     ///< TPDO mapping when true.
    std::array<EdsMappingEntry, 8> entries{}; ///< Mapping entries.
    std::uint8_t count{0};                    ///< Valid entries.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line mapping summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("EdsPdoMapping(index=0x{:04x}, pdo={}, transmit={}, count={})",
                           mapping_index, pdo, transmit, count);
    }
};

/// @brief Free helper mirroring `EdsPdoMapping::to_string`.
[[nodiscard]] inline std::string to_string(const EdsPdoMapping& mapping) {
    return mapping.to_string();
}

} // namespace nexigen::canopen
