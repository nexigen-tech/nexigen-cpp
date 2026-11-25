// nexigen-canview — NMT command and heartbeat decoding.
#pragma once

#include <cstdint>

#include <nexigen/hw/can/can_message.hpp>

#include "canopen_registry.hpp"
#include "co_decoded.hpp"

namespace nexigen::canview {

/// @brief Decode an NMT command (`0x000`) and update the registry.
/// @param message Frame.
/// @param registry Binding registry to update.
/// @param out Decoded output.
/// @return `true` when the frame is an NMT command.
bool decode_nmt(const nexigen::hw::can::CanMessage& message, CanOpenRegistry& registry,
                CoDecoded& out) noexcept;

/// @brief Decode a heartbeat/boot-up (`0x700 + node`).
/// @param message Frame.
/// @param registry Binding registry to update.
/// @param out Decoded output.
/// @param now_ns Host monotonic time.
/// @return `true` when the frame is a heartbeat/boot-up.
bool decode_heartbeat(const nexigen::hw::can::CanMessage& message, CanOpenRegistry& registry,
                      CoDecoded& out, std::uint64_t now_ns) noexcept;

/// @brief Map a heartbeat state byte to an NMT state.
/// @param state Heartbeat state.
/// @return NMT state.
[[nodiscard]] NmtState nmt_state_from_byte(std::uint8_t state) noexcept;

} // namespace nexigen::canview
