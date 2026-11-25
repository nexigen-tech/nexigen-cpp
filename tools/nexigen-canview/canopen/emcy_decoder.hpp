// nexigen-canview — EMCY decoding.
#pragma once

#include <cstdint>
#include <string_view>

#include <nexigen/hw/can/can_message.hpp>

#include "co_decoded.hpp"

namespace nexigen::canview {

/// @brief Canonical name of a common emergency error code.
/// @param code 16-bit EMCY code.
/// @return Static name, or an empty view for unknown codes.
[[nodiscard]] std::string_view emcy_code_name(std::uint16_t code) noexcept;

/// @brief Decode an EMCY frame (`0x080 + node`, node > 0).
/// @param message Frame.
/// @param out Decoded output.
/// @return `true` when the frame is an EMCY frame.
bool decode_emcy(const nexigen::hw::can::CanMessage& message, CoDecoded& out) noexcept;

} // namespace nexigen::canview
