// nexigen::canopen — completed SDO write.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::canopen {

/// @brief One successful SDO write observed by the assembler.
struct SdoCompletedWrite {
    std::uint16_t index{0};                ///< Object index.
    std::uint8_t subindex{0};              ///< Object subindex.
    std::array<std::uint8_t, 4096> data{}; ///< Written payload.
    std::size_t size{0};                   ///< Payload byte count.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line write summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("SdoCompletedWrite(index=0x{:04x}, sub={}, size={})", index, subindex,
                           size);
    }
};

/// @brief Free helper mirroring `SdoCompletedWrite::to_string`.
[[nodiscard]] inline std::string to_string(const SdoCompletedWrite& write) {
    return write.to_string();
}

/// @brief Serialize one completed SDO write.
void to_json(nlohmann::json& j, const SdoCompletedWrite& write);
/// @brief Parse one completed SDO write without throwing.
void from_json(const nlohmann::json& j, SdoCompletedWrite& write) noexcept;

} // namespace nexigen::canopen
