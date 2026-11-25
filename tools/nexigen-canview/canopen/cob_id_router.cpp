#include "cob_id_router.hpp"

#include <format>

namespace nexigen::canview {

void CobIdRouter::clear() noexcept {
    for (auto& channel : routes_) {
        for (auto& slot : channel) {
            slot.used = false;
        }
    }
    conflicts_.clear();
}

void CobIdRouter::set_route(std::uint16_t channel_id, std::uint16_t can_id,
                            const CobIdRoute& route) {
    if (can_id >= max_cob_ids) {
        return;
    }
    Slot& slot = routes_[channel_id % channels][can_id];
    if (slot.used && slot.route.slot != route.slot) {
        CobIdConflict conflict{};
        conflict.channel_id = channel_id;
        conflict.can_id = can_id;
        conflict.kept_slot = slot.route.slot;
        conflict.rejected_slot = route.slot;
        conflict.service = route.service;
        conflicts_.push_back(conflict);
    }
    slot.route = route;
    slot.used = true;
}

void CobIdRouter::erase_route(std::uint16_t channel_id, std::uint16_t can_id) noexcept {
    if (can_id >= max_cob_ids) {
        return;
    }
    routes_[channel_id % channels][can_id].used = false;
}

void CobIdRouter::add_route(std::uint16_t channel_id, std::uint16_t can_id,
                            const CobIdRoute& route) {
    if (can_id >= max_cob_ids) {
        return;
    }
    Slot& slot = routes_[channel_id % channels][can_id];
    if (!slot.used) {
        slot.route = route;
        slot.used = true;
        return;
    }
    if (slot.route.slot == route.slot && slot.route.service == route.service &&
        slot.route.pdo == route.pdo) {
        return;
    }
    if (slot.route.slot == route.slot) {
        return; // same binding already owns the identifier
    }
    CobIdConflict conflict{};
    conflict.channel_id = channel_id;
    conflict.can_id = can_id;
    conflict.kept_slot = slot.route.slot;
    conflict.rejected_slot = route.slot;
    conflict.service = route.service;
    conflicts_.push_back(conflict);
}

void CobIdRouter::add_custom(std::uint16_t channel_id, const NodeBinding& binding,
                             std::array<std::array<bool, 5>, 2>& custom_pdo) {
    if (binding.dictionary == nullptr) {
        return;
    }
    for (const EdsCobIdEntry& entry : binding.dictionary->cob_ids()) {
        if (entry.pdo == 0U || entry.pdo > 4U) {
            continue;
        }
        if (entry.disabled) {
            // A disabled PDO keeps its default route suppressed as well.
            custom_pdo[entry.transmit ? 1U : 0U][entry.pdo] = true;
            continue;
        }
        const std::uint32_t resolved = entry.cob_id.resolve(binding.node_id);
        if (resolved > 0x7FFU) {
            continue; // CANopen is 11-bit only
        }
        CobIdRoute route{};
        route.slot = binding.registry_slot;
        route.node_id = binding.node_id;
        route.service = entry.transmit ? CoService::Tpdo : CoService::Rpdo;
        route.pdo = entry.pdo;
        route.custom = true;
        add_route(channel_id, static_cast<std::uint16_t>(resolved), route);
        custom_pdo[entry.transmit ? 1U : 0U][entry.pdo] = true;
    }
}

void CobIdRouter::add_defaults(std::uint16_t channel_id, const NodeBinding& binding,
                               const std::array<std::array<bool, 5>, 2>& custom_pdo) {
    const auto node = static_cast<std::uint16_t>(binding.node_id);
    CobIdRoute route{};
    route.slot = binding.registry_slot;
    route.node_id = binding.node_id;

    route.service = CoService::Emcy;
    add_route(channel_id, static_cast<std::uint16_t>(0x080U + node), route);

    route.service = CoService::SdoResponse;
    add_route(channel_id, static_cast<std::uint16_t>(0x580U + node), route);

    route.service = CoService::SdoRequest;
    add_route(channel_id, static_cast<std::uint16_t>(0x600U + node), route);

    route.service = CoService::Heartbeat;
    add_route(channel_id, static_cast<std::uint16_t>(0x700U + node), route);

    for (std::uint8_t pdo = 1U; pdo <= 4U; ++pdo) {
        route.service = CoService::Tpdo;
        route.pdo = pdo;
        if (!custom_pdo[1U][pdo]) {
            add_route(channel_id, static_cast<std::uint16_t>(0x180U + (pdo - 1U) * 0x100U + node),
                      route);
        }
        route.service = CoService::Rpdo;
        if (!custom_pdo[0U][pdo]) {
            add_route(channel_id, static_cast<std::uint16_t>(0x200U + (pdo - 1U) * 0x100U + node),
                      route);
        }
    }
}

void CobIdRouter::rebuild(const CanOpenRegistry& registry) {
    clear();
    std::array<bool, channels> globals{};
    for (std::uint16_t slot = 0; slot < CanOpenRegistry::capacity; ++slot) {
        const NodeBinding* binding = registry.at(slot);
        if (binding == nullptr) {
            continue;
        }
        if (!globals[binding->channel_id % channels]) {
            globals[binding->channel_id % channels] = true;
            CobIdRoute nmt{};
            nmt.service = CoService::Nmt;
            add_route(binding->channel_id, 0x000U, nmt);
            CobIdRoute sync{};
            sync.service = CoService::Sync;
            add_route(binding->channel_id, 0x080U, sync);
            CobIdRoute time{};
            time.service = CoService::Time;
            add_route(binding->channel_id, 0x100U, time);
        }
        std::array<std::array<bool, 5>, 2> custom_pdo{};
        add_custom(binding->channel_id, *binding, custom_pdo);
        add_defaults(binding->channel_id, *binding, custom_pdo);
    }
}

std::optional<CobIdRoute> CobIdRouter::lookup(std::uint16_t channel_id, std::uint32_t can_id,
                                              bool extended) const noexcept {
    if (extended || can_id >= max_cob_ids) {
        return std::nullopt;
    }
    const Slot& slot = routes_[channel_id % channels][can_id];
    if (!slot.used) {
        return std::nullopt;
    }
    return slot.route;
}

std::string CobIdRouter::to_string() const {
    std::size_t routes = 0U;
    for (const auto& channel : routes_) {
        for (const auto& slot : channel) {
            if (slot.used) {
                ++routes;
            }
        }
    }
    return std::format("CobIdRouter(routes={}, conflicts={})", routes, conflicts_.size());
}

} // namespace nexigen::canview
