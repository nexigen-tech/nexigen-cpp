#include <nexigen/hw/can/can_channel_tracker.hpp>

namespace nexigen::hw::can {
namespace {

CanChannelState state_from_flags(const CanErrorFlags& flags) noexcept {
    if (flags.has(CanErrorFlags::bus_off)) {
        return CanChannelState::BusOff;
    }
    if (flags.has(CanErrorFlags::error_passive)) {
        return CanChannelState::ErrorPassive;
    }
    if (flags.has(CanErrorFlags::error_warning)) {
        return CanChannelState::Warning;
    }
    return CanChannelState::Active;
}

} // namespace

void CanChannelTracker::set_state(CanChannelState state) noexcept {
    if (state == last_state_) {
        return;
    }
    if (state == CanChannelState::BusOff && last_state_ != CanChannelState::BusOff) {
        ++status_.bus_off_count;
    }
    status_.state = state;
    last_state_ = state;
    state_changed_ = true;
}

void CanChannelTracker::observe(const CanMessage& message) noexcept {
    status_.timestamp_ns = message.hardware_timestamp_ns;
    status_.last_error_flags = message.error_flags;

    if (message.kind == CanMessageKind::Error) {
        ++status_.error_frame_count;
    } else if (message.kind != CanMessageKind::BusIdle) {
        ++status_.rx_count;
    }

    if (message.frame_flags.has(CanFrameFlags::has_hardware_sequence)) {
        if (has_sequence_) {
            if (message.hardware_sequence > last_sequence_ + 1U) {
                status_.sequence_gap_count += message.hardware_sequence - last_sequence_ - 1U;
            }
        }
        last_sequence_ = message.hardware_sequence;
        has_sequence_ = true;
    }

    if (message.error_flags.any()) {
        set_state(state_from_flags(message.error_flags));
    } else if (last_state_ != CanChannelState::Unknown) {
        set_state(CanChannelState::Active);
    }
}

} // namespace nexigen::hw::can
