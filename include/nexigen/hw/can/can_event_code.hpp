#pragma once

#include <cstdint>

namespace nexigen::hw::can {

/// @brief Event code for a received CAN message.
inline constexpr std::uint32_t event_rx = 0x2100U;
/// @brief Event code for a transmission request.
inline constexpr std::uint32_t event_tx = 0x2101U;
/// @brief Event code for an error frame.
inline constexpr std::uint32_t event_error = 0x2102U;
/// @brief Event code for an overload frame.
inline constexpr std::uint32_t event_overload = 0x2103U;
/// @brief Event code for a bus-idle heartbeat.
inline constexpr std::uint32_t event_bus_idle = 0x2104U;
/// @brief Event code for a channel status message.
inline constexpr std::uint32_t event_status = 0x2105U;

} // namespace nexigen::hw::can
