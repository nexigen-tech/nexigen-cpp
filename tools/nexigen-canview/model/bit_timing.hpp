// nexigen-canview — CAN frame bit-time estimation.
#pragma once

#include <cstdint>

#include <nexigen/hw/can/can_message.hpp>

namespace nexigen::canview {

/// @brief Timing configuration of one channel.
struct ChannelTiming {
    std::uint32_t nominal_bitrate{0}; ///< Nominal bitrate in bit/s; 0 = unknown.
    std::uint32_t data_bitrate{0};    ///< CAN FD data-phase bitrate; 0 = unknown.
    bool fd{false};                   ///< Channel accepts CAN FD.
};

/// @brief Estimated on-wire occupancy of one frame.
struct FrameTiming {
    bool available{false};    ///< Whether a configured nominal bitrate exists.
    std::uint64_t bits{0};    ///< Estimated raw bits.
    std::uint64_t time_ns{0}; ///< Estimated bus time.
};

/// @brief Estimate the bus occupancy of one message.
/// @param message Frame.
/// @param timing Channel timing.
/// @return Bit count and duration; `available=false` without a nominal bitrate.
[[nodiscard]] FrameTiming estimate_frame_timing(const nexigen::hw::can::CanMessage& message,
                                                const ChannelTiming& timing) noexcept;

} // namespace nexigen::canview
