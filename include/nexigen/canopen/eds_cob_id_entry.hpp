// nexigen::canopen — one EDS COB-ID override.
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nexigen/canopen/eds_numeric.hpp>

namespace nexigen::canopen {

/// @brief One EDS-provided COB-ID override.
struct EdsCobIdEntry {
    std::uint16_t comm_index{0}; ///< Communication parameter index.
    std::uint8_t pdo{0};         ///< 1..4.
    bool transmit{false};        ///< TPDO COB-ID when true.
    EdsNumeric cob_id{};         ///< Raw value, maybe node-relative.
    bool disabled{false};        ///< Bit 31 set: PDO disabled.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line COB-ID summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("EdsCobIdEntry(comm=0x{:04x}, pdo={}, transmit={}, disabled={})",
                           comm_index, pdo, transmit, disabled);
    }
};

/// @brief Free helper mirroring `EdsCobIdEntry::to_string`.
[[nodiscard]] inline std::string to_string(const EdsCobIdEntry& entry) {
    return entry.to_string();
}

} // namespace nexigen::canopen
