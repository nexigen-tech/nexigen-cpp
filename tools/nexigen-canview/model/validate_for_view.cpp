#include "validate_for_view.hpp"

namespace nexigen::canview {

namespace hwcan = nexigen::hw::can;

namespace {

bool check_length(const hwcan::CanMessage& message, const char*& reason) noexcept {
    switch (message.kind) {
        case hwcan::CanMessageKind::ClassicData:
            if (message.dlc > 8U) {
                reason = "classic length exceeds 8 bytes";
                return false;
            }
            return true;
        case hwcan::CanMessageKind::FdData:
            if (!is_legal_fd_length(message.dlc)) {
                reason = "illegal CAN FD payload length";
                return false;
            }
            return true;
        case hwcan::CanMessageKind::Remote:
            if (message.dlc > 8U) {
                reason = "remote requested length exceeds 8 bytes";
                return false;
            }
            return true;
        case hwcan::CanMessageKind::Error:
        case hwcan::CanMessageKind::Overload:
            if (message.dlc != 0U) {
                reason = "error/overload record carries a payload";
                return false;
            }
            return true;
        case hwcan::CanMessageKind::BusIdle:
            if (message.dlc != 0U || message.can_id != 0U) {
                reason = "bus-idle record carries an identifier or payload";
                return false;
            }
            return true;
    }
    reason = "unknown message kind";
    return false;
}

bool check_identifier(const hwcan::CanMessage& message, const char*& reason) noexcept {
    if (message.extended && message.can_id > 0x1FFFFFFFU) {
        reason = "29-bit identifier out of range";
        return false;
    }
    if (!message.extended && message.can_id > 0x7FFU) {
        reason = "11-bit identifier out of range";
        return false;
    }
    return true;
}

} // namespace

ViewValidation validate_for_view(const hwcan::CanMessage& message) noexcept {
    ViewValidation result{};
    result.timestamp_usable = message.frame_flags.has(hwcan::CanFrameFlags::has_hardware_timestamp);
    result.sequence_usable = message.frame_flags.has(hwcan::CanFrameFlags::has_hardware_sequence);
    result.valid = check_length(message, result.reason) && check_identifier(message, result.reason);
    return result;
}

} // namespace nexigen::canview
