// nexigen-canview — per-channel COB-ID routing.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "canopen_registry.hpp"
#include "canopen_types.hpp"

namespace nexigen::canview {

/// @brief Route resolved from one 11-bit COB-ID.
struct CobIdRoute {
    static constexpr std::uint16_t broadcast{0xFFFFU}; ///< Broadcast route slot.

    std::uint16_t slot{broadcast};         ///< Registry slot, or `broadcast`.
    std::uint8_t node_id{0};               ///< Bound node id.
    CoService service{CoService::Unknown}; ///< Decoded service.
    std::uint8_t pdo{0};                   ///< PDO number for TPDO/RPDO.
    bool custom{false};                    ///< Route came from an EDS COB-ID override.
};

/// @brief One COB-ID conflict between two bindings.
struct CobIdConflict {
    std::uint16_t channel_id{0};           ///< Channel identity.
    std::uint16_t can_id{0};               ///< Contested 11-bit identifier.
    std::uint16_t kept_slot{0};            ///< Binding that owns the route.
    std::uint16_t rejected_slot{0};        ///< Binding whose route was rejected.
    CoService service{CoService::Unknown}; ///< Contested service.
};

/// @brief Per-channel 2 048-entry COB-ID route table.
class CobIdRouter {
public:
    /// @brief Addressable channels.
    static constexpr std::size_t channels{32U};
    /// @brief Addressable 11-bit identifiers.
    static constexpr std::size_t max_cob_ids{2048U};

    /// @brief Rebuild every route from the registry.
    /// @param registry Current bindings.
    void rebuild(const CanOpenRegistry& registry);

    /// @brief Resolve one frame identifier.
    /// @param channel_id Channel identity.
    /// @param can_id CAN identifier.
    /// @param extended Whether the frame uses a 29-bit identifier.
    /// @return Route, or `std::nullopt` for unbound/extended/unknown IDs.
    [[nodiscard]] std::optional<CobIdRoute> lookup(std::uint16_t channel_id, std::uint32_t can_id,
                                                   bool extended) const noexcept;

    /// @brief Conflicts discovered by the last rebuild.
    /// @return Borrowed vector.
    [[nodiscard]] const std::vector<CobIdConflict>& conflicts() const noexcept {
        return conflicts_;
    }

    /// @brief Install or replace one dynamic route.
    /// @param channel_id Channel identity.
    /// @param can_id 11-bit COB-ID.
    /// @param route Route to install.
    void set_route(std::uint16_t channel_id, std::uint16_t can_id, const CobIdRoute& route);

    /// @brief Remove one dynamic route.
    /// @param channel_id Channel identity.
    /// @param can_id 11-bit COB-ID.
    void erase_route(std::uint16_t channel_id, std::uint16_t can_id) noexcept;

    /// @brief Drop every route.
    void clear() noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with route and conflict counts.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief One route slot.
    struct Slot {
        CobIdRoute route{}; ///< Route.
        bool used{false};   ///< Whether the identifier is routed.
    };

    /// @brief Add one route, keeping the first on conflict.
    void add_route(std::uint16_t channel_id, std::uint16_t can_id, const CobIdRoute& route);
    /// @brief Add node defaults not overridden by a custom COB-ID.
    void add_defaults(std::uint16_t channel_id, const NodeBinding& binding,
                      const std::array<std::array<bool, 5>, 2>& custom_pdo);
    /// @brief Add EDS custom COB-ID overrides.
    /// @param custom_pdo Receives the overridden PDO slots.
    void add_custom(std::uint16_t channel_id, const NodeBinding& binding,
                    std::array<std::array<bool, 5>, 2>& custom_pdo);

    std::array<std::array<Slot, max_cob_ids>, channels> routes_{}; ///< Route storage.
    std::vector<CobIdConflict> conflicts_{};                       ///< Last conflicts.
};

} // namespace nexigen::canview
