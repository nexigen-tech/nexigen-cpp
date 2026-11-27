// nexigen::canopen — one COB-ID conflict.
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/canopen/co_service.hpp>

namespace nexigen::canopen {

/// @brief One COB-ID conflict between two bindings.
struct CobIdConflict {
    std::uint16_t channel_id{0};           ///< Channel identity.
    std::uint16_t can_id{0};               ///< Contested 11-bit identifier.
    std::uint16_t kept_slot{0};            ///< Binding that owns the route.
    std::uint16_t rejected_slot{0};        ///< Binding whose route was rejected.
    CoService service{CoService::Unknown}; ///< Contested service.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line conflict summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("CobIdConflict(channel={}, can_id=0x{:03x}, kept={}, rejected={})",
                           channel_id, can_id, kept_slot, rejected_slot);
    }
};

/// @brief Free helper mirroring `CobIdConflict::to_string`.
[[nodiscard]] inline std::string to_string(const CobIdConflict& conflict) {
    return conflict.to_string();
}

/// @brief Serialize one COB-ID conflict.
void to_json(nlohmann::json& j, const CobIdConflict& conflict);
/// @brief Parse one COB-ID conflict without throwing.
void from_json(const nlohmann::json& j, CobIdConflict& conflict) noexcept;

} // namespace nexigen::canopen
