// nexigen::canopen — one resolved COB-ID route.
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/canopen/co_service.hpp>

namespace nexigen::canopen {

/// @brief Route resolved from one 11-bit COB-ID.
struct CobIdRoute {
    static constexpr std::uint16_t broadcast{0xFFFFU}; ///< Broadcast route slot.

    std::uint16_t slot{broadcast};         ///< Opaque owner slot, or `broadcast`.
    std::uint8_t node_id{0};               ///< Bound node id.
    CoService service{CoService::Unknown}; ///< Decoded service.
    std::uint8_t pdo{0};                   ///< PDO number for TPDO/RPDO.
    bool custom{false};                    ///< Route came from an EDS COB-ID override.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line route summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("CobIdRoute(slot={}, node={}, service={}, pdo={}, custom={})", slot,
                           node_id, nexigen::canopen::to_string(service), pdo, custom);
    }
};

/// @brief Free helper mirroring `CobIdRoute::to_string`.
[[nodiscard]] inline std::string to_string(const CobIdRoute& route) {
    return route.to_string();
}

/// @brief Serialize one COB-ID route.
void to_json(nlohmann::json& j, const CobIdRoute& route);
/// @brief Parse one COB-ID route without throwing.
void from_json(const nlohmann::json& j, CobIdRoute& route) noexcept;

} // namespace nexigen::canopen
