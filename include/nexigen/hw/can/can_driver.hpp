#pragma once

#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_channel_config.hpp>
#include <nexigen/hw/can/can_driver_capabilities.hpp>
#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/hw/can/can_message_batch.hpp>
#include <nexigen/hw/can/i_can_message_sink.hpp>

namespace nexigen::hw::can {

/// @brief Vendor-neutral CAN driver contract.
///
/// The concrete hardware adapter is supplied by the interface module. This
/// contract deliberately names no operating-system CAN API.
class CanDriver {
public:
    /// @brief Virtual destructor for the driver adapter.
    virtual ~CanDriver() = default;

    /// @brief Report supported features and channel limits.
    /// @return Capabilities, or a driver error.
    [[nodiscard]] virtual core::Result<CanDriverCapabilities> capabilities() noexcept = 0;

    /// @brief Open one configured channel and bind its message sink.
    /// @param config Channel configuration.
    /// @param sink Receiver that must outlive the open channel.
    /// @return OK, or a driver/validation error.
    [[nodiscard]] virtual core::Status open_channel(const CanChannelConfig& config,
                                                    ICanMessageSink& sink) noexcept = 0;

    /// @brief Submit one message for transmission.
    /// @param channel Logical channel name.
    /// @param message Message to transmit.
    /// @return OK, `BufferFull`, `UnsupportedFormat`, or a driver error.
    [[nodiscard]] virtual core::Status submit(std::string_view channel,
                                              const CanMessage& message) noexcept = 0;

    /// @brief Submit a bounded batch of messages.
    ///
    /// The default implementation validates the batch and submits every
    /// contained message individually. A driver that supports batch transport
    /// overrides this method and reports `batch_submission` in its capabilities.
    ///
    /// @param channel Logical channel name.
    /// @param batch Validated batch envelope.
    /// @return OK, or the first per-message failure.
    [[nodiscard]] virtual core::Status submit_batch(std::string_view channel,
                                                    const CanMessageBatch& batch) noexcept {
        if (const core::Status valid = batch.validate(); !valid) {
            return valid;
        }
        for (std::size_t index = 0; index < batch.count; ++index) {
            if (const core::Status submitted = submit(channel, batch.messages[index]); !submitted) {
                return submitted;
            }
        }
        return {};
    }

    /// @brief Close one channel and release its sink binding.
    /// @param channel Logical channel name.
    /// @return OK, `NotFound`, or a driver error.
    [[nodiscard]] virtual core::Status close_channel(std::string_view channel) noexcept = 0;
};

} // namespace nexigen::hw::can
