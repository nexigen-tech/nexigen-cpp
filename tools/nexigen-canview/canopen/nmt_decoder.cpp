#include "nmt_decoder.hpp"

#include <cstdio>

namespace nexigen::canview {

namespace {

NmtState command_state(std::uint8_t command) noexcept {
    switch (command) {
        case 1U:    return NmtState::Operational;
        case 2U:    return NmtState::Stopped;
        case 0x80U: return NmtState::PreOperational;
        case 0x81U:
        case 0x82U: return NmtState::Unknown;
        default:    return NmtState::Unknown;
    }
}

const char* command_name(std::uint8_t command) noexcept {
    switch (command) {
        case 1U:    return "start";
        case 2U:    return "stop";
        case 0x80U: return "pre-operational";
        case 0x81U: return "reset node";
        case 0x82U: return "reset communication";
        default:    return "unknown";
    }
}

} // namespace

NmtState nmt_state_from_byte(std::uint8_t state) noexcept {
    switch (state) {
        case 0U:   return NmtState::BootUp;
        case 4U:   return NmtState::Stopped;
        case 5U:   return NmtState::Operational;
        case 127U: return NmtState::PreOperational;
        default:   return NmtState::Unknown;
    }
}

bool decode_nmt(const nexigen::hw::can::CanMessage& message, CanOpenRegistry& registry,
                CoDecoded& out) noexcept {
    if (message.can_id != 0x000U || message.dlc < 2U) {
        return false;
    }
    const std::uint8_t command = message.data[0];
    const std::uint8_t target = message.data[1];
    const NmtState state = command_state(command);
    out.service = CoService::Nmt;
    out.status = CoDecodeStatus::Ok;
    out.nmt_state = state;
    out.node_id = target;
    if (target == 0U) {
        for (std::uint16_t slot = 0; slot < CanOpenRegistry::capacity; ++slot) {
            const NodeBinding* binding = registry.at(slot);
            if (binding != nullptr && binding->channel_id == message.channel_id) {
                registry.set_nmt_state(slot, state);
            }
        }
    } else {
        const NodeBinding* binding = registry.find(message.channel_id, target);
        if (binding != nullptr) {
            registry.set_nmt_state(binding->registry_slot, state);
            out.slot = binding->registry_slot;
        }
    }
    char text[96]{};
    std::snprintf(text, sizeof(text), "NMT %s -> %s", command_name(command),
                  std::string(to_string(state)).c_str());
    out.set_text(text);
    return true;
}

bool decode_heartbeat(const nexigen::hw::can::CanMessage& message, CanOpenRegistry& registry,
                      CoDecoded& out, std::uint64_t now_ns) noexcept {
    if (message.can_id < 0x701U || message.can_id > 0x77FU || message.dlc < 1U) {
        return false;
    }
    const auto node_id = static_cast<std::uint8_t>(message.can_id - 0x700U);
    const NodeBinding* binding = registry.find(message.channel_id, node_id);
    out.service = CoService::Heartbeat;
    out.status = CoDecodeStatus::Ok;
    out.node_id = node_id;
    out.nmt_state = nmt_state_from_byte(message.data[0]);
    if (binding != nullptr) {
        out.slot = binding->registry_slot;
        registry.observe_heartbeat(binding->registry_slot, out.nmt_state, now_ns);
    }
    char text[64]{};
    std::snprintf(text, sizeof(text), "heartbeat node %u: %s", node_id,
                  std::string(to_string(out.nmt_state)).c_str());
    out.set_text(text);
    return true;
}

} // namespace nexigen::canview
