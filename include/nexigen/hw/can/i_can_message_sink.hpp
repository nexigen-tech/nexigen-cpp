#pragma once

#include <nexigen/hw/can/can_message.hpp>

namespace nexigen::hw::can {

/// @brief Non-allocating receiver of the driver message stream.
class ICanMessageSink {
public:
    /// @brief Virtual destructor for the driver-owned callback.
    virtual ~ICanMessageSink() = default;

    /// @brief Deliver one message from the driver thread.
    /// @param message Message with all hardware metadata.
    /// @note The callback must not allocate, block or throw.
    virtual void on_message(const CanMessage& message) noexcept = 0;
};

} // namespace nexigen::hw::can
