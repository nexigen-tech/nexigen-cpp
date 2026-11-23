#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/hw/can/can_channel_state.hpp>
#include <nexigen/hw/can/can_error_flags.hpp>

namespace nexigen::hw::can {

/// @brief Health snapshot of one CAN channel.
struct CanChannelStatus {
    CanChannelState state{CanChannelState::Unknown}; ///< Current state.
    std::uint32_t tx_error_counter{0};               ///< Transmit error counter.
    std::uint32_t rx_error_counter{0};               ///< Receive error counter.
    std::uint32_t bus_off_count{0};                  ///< Bus-off transitions.
    std::uint32_t error_frame_count{0};              ///< Error frames observed.
    std::uint64_t rx_count{0};                       ///< Received messages.
    std::uint64_t tx_count{0};                       ///< Transmitted messages.
    std::uint64_t dropped_count{0};                  ///< Dropped messages.
    std::uint64_t sequence_gap_count{0};             ///< Detected sequence gaps.
    CanErrorFlags last_error_flags{};                ///< Last reported error flags.
    std::uint64_t timestamp_ns{0};                   ///< Status timestamp.

    /// @brief Render the status for cold-path diagnostics.
    /// @return Human-readable summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `CanChannelStatus::to_string`.
/// @param status Status to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const CanChannelStatus& status) {
    return status.to_string();
}

/// @brief Serialize a status for cold-path diagnostics.
/// @param j Destination.
/// @param status Status to serialize.
void to_json(nlohmann::json& j, const CanChannelStatus& status);

/// @brief Parse a status from JSON without throwing.
/// @param j Source.
/// @param status Destination.
void from_json(const nlohmann::json& j, CanChannelStatus& status) noexcept;

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanChannelStatus`.
template <>
struct std::formatter<nexigen::hw::can::CanChannelStatus> : std::formatter<std::string_view> {
    /// @brief Format the status through `to_string`.
    /// @tparam Ctx Format context.
    /// @param status Status to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx>
    auto format(const nexigen::hw::can::CanChannelStatus& status, Ctx& ctx) const {
        const std::string text = status.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
