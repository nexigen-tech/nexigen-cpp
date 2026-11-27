// nexigen::canopen — per-channel COB-ID routing.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <nexigen/canopen/cob_id_binding.hpp>
#include <nexigen/canopen/cob_id_conflict.hpp>
#include <nexigen/canopen/cob_id_route.hpp>

namespace nexigen::canopen {

/// @brief Per-channel 2 048-entry COB-ID route table.
class CobIdRouter {
public:
    /// @brief Addressable channels.
    static constexpr std::size_t channels{32U};
    /// @brief Addressable 11-bit identifiers.
    static constexpr std::size_t max_cob_ids{2048U};

    /// @brief Rebuild every route from a neutral binding list.
    /// @param bindings Current node bindings.
    void rebuild(std::span<const CobIdBinding> bindings);

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
    void add_defaults(std::uint16_t channel_id, const CobIdBinding& binding,
                      const std::array<std::array<bool, 5>, 2>& custom_pdo);
    /// @brief Add EDS custom COB-ID overrides.
    /// @param custom_pdo Receives the overridden PDO slots.
    void add_custom(std::uint16_t channel_id, const CobIdBinding& binding,
                    std::array<std::array<bool, 5>, 2>& custom_pdo);

    std::array<std::array<Slot, max_cob_ids>, channels> routes_{}; ///< Route storage.
    std::vector<CobIdConflict> conflicts_{};                       ///< Last conflicts.
};

} // namespace nexigen::canopen
