// nexigen-canview — viewer-local CAN/CAN-FD structural validation.
#pragma once

#include <cstdint>

#include <nexigen/hw/can/can_message.hpp>

namespace nexigen::canview {

/// @brief Result of the viewer's structural checks.
///
/// `valid` covers kind, length and identifier rules. The two presence flags are
/// reported separately: an otherwise valid frame without a hardware timestamp
/// or sequence is retained but cannot participate in time or gap analysis.
struct ViewValidation {
    bool valid{true};             ///< Kind, DLC and identifier rules passed.
    bool timestamp_usable{false}; ///< `has_hardware_timestamp` is set.
    bool sequence_usable{false};  ///< `has_hardware_sequence` is set.
    const char* reason{nullptr};  ///< Static reason when `valid` is false.
};

/// @brief Whether a CAN FD payload length is legal.
/// @param dlc Data length code.
/// @return `true` for 0..8, 12, 16, 20, 24, 32, 48 and 64.
[[nodiscard]] constexpr bool is_legal_fd_length(std::uint8_t dlc) noexcept {
    return dlc <= 8U || dlc == 12U || dlc == 16U || dlc == 20U || dlc == 24U || dlc == 32U ||
           dlc == 48U || dlc == 64U;
}

/// @brief Apply the viewer's structural rules without modifying the message.
///
/// Unlike the transmit validator, a `Remote` frame accepts a requested length
/// of 0..8. `FdData` accepts only legal CAN FD lengths. Error, overload and
/// bus-idle records keep the library rules.
///
/// @param message Message to inspect.
/// @return Validation result with static reason text on failure.
[[nodiscard]] ViewValidation
validate_for_view(const nexigen::hw::can::CanMessage& message) noexcept;

} // namespace nexigen::canview
