#pragma once

#include <span>

#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_channel_config.hpp>

namespace nexigen::hw::can {

/// @brief Validate a process-wide channel list.
/// @param channels Configurations in declaration order.
/// @return OK, or `InvalidArgument` for an invalid, duplicate or seventeenth channel.
[[nodiscard]] core::Status validate_channels(std::span<const CanChannelConfig> channels) noexcept;

} // namespace nexigen::hw::can
