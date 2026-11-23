#pragma once

#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_driver.hpp>

namespace nexigen::hw::can {

/// @brief Skeleton adapter for the vendor CAN interface library.
///
/// Fill the TODO bodies with the vendor SDK calls. The rest of the CAN stack
/// (`CanReceiver`, `CanTransmitter`, topic mapping and tools) already consumes
/// this contract and does not need to change.
///
/// @note The hardware adapter is integration-owned; this header is a compile
///       scaffold, not a production driver.
class VendorCanDriver final : public CanDriver {
public:
    /// @brief Report the vendor adapter's capabilities.
    /// @return Capabilities; replace with the values reported by the SDK.
    [[nodiscard]] core::Result<CanDriverCapabilities> capabilities() noexcept override {
        CanDriverCapabilities caps{};
        caps.supports_fd = true;
        caps.supports_remote = true;
        caps.hardware_timestamps = true;
        caps.hardware_sequence = true;
        caps.error_frames = true;
        caps.overload_frames = true;
        caps.heartbeats = true;
        caps.max_channels = 16;
        return caps;
    }

    /// @brief Open a vendor channel and register @p sink.
    /// @param config Channel configuration.
    /// @param sink Receiver of the vendor driver stream.
    /// @return TODO: return the SDK open status.
    [[nodiscard]] core::Status open_channel(const CanChannelConfig& config,
                                            ICanMessageSink& sink) noexcept override {
        static_cast<void>(config);
        static_cast<void>(sink);
        // TODO: call vendor_open(config.driver_channel, ...) and keep the sink
        //       for every vendor callback. The callback must preserve hardware
        //       timestamps, sequence numbers, error flags and heartbeats.
        return std::unexpected(core::ErrorCode::NotInitialized);
    }

    /// @brief Submit one frame through the vendor library.
    /// @param channel Logical channel name.
    /// @param message Frame to transmit.
    /// @return TODO: return the SDK write status.
    [[nodiscard]] core::Status submit(std::string_view channel,
                                      const CanMessage& message) noexcept override {
        static_cast<void>(channel);
        static_cast<void>(message);
        // TODO: call vendor_write(...). Return BufferFull/SystemError so the
        //       transmitter can apply its bounded retry policy.
        return std::unexpected(core::ErrorCode::NotInitialized);
    }

    /// @brief Close one vendor channel.
    /// @param channel Logical channel name.
    /// @return TODO: return the SDK close status.
    [[nodiscard]] core::Status close_channel(std::string_view channel) noexcept override {
        static_cast<void>(channel);
        // TODO: call vendor_close(...) and clear the channel's sink binding.
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
};

} // namespace nexigen::hw::can
