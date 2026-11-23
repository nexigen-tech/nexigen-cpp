#pragma once

#include <cstdint>

#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_message.hpp>

namespace nexigen::hw::can {

/// @brief Accumulate channel counters and state transitions from a message stream.
class CanChannelTracker {
public:
    /// @brief Start from an empty status snapshot.
    CanChannelTracker() noexcept = default;

    /// @brief Apply one driver message.
    /// @param message Message with hardware sequence and error flags.
    void observe(const CanMessage& message) noexcept;

    /// @brief Apply an explicit driver state transition.
    /// @param state New channel state.
    void set_state(CanChannelState state) noexcept;

    /// @brief Current status snapshot.
    /// @return Borrowed status value.
    [[nodiscard]] const CanChannelStatus& status() const noexcept { return status_; }

    /// @brief Consume the state-change latch.
    /// @return `true` once after a state transition.
    [[nodiscard]] bool consume_state_changed() noexcept {
        const bool changed = state_changed_;
        state_changed_ = false;
        return changed;
    }

private:
    CanChannelStatus status_{};
    std::uint64_t last_sequence_{0};
    bool has_sequence_{false};
    CanChannelState last_state_{CanChannelState::Unknown};
    bool state_changed_{false};
};

} // namespace nexigen::hw::can
