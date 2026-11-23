#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/core/detail/json_field.hpp>

#include <algorithm>
#include <limits>

namespace nexigen::hw::can {

core::Status CanMessage::validate() const noexcept {
    if (kind == CanMessageKind::ClassicData && dlc > 8U) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (kind == CanMessageKind::FdData && dlc > max_payload) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if ((kind == CanMessageKind::Remote || kind == CanMessageKind::Error ||
         kind == CanMessageKind::Overload || kind == CanMessageKind::BusIdle) &&
        dlc != 0U) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (kind == CanMessageKind::BusIdle && can_id != 0U) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (extended) {
        if (can_id > 0x1FFFFFFFU) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
    } else if (can_id > 0x7FFU) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

std::string CanMessage::to_string() const {
    return std::format(
        "CanMessage(kind={}, channel={}, id=0x{:x}, dlc={}, origin={}, timestamp_source={}, "
        "error_flags={}, timestamp_ns={}, sequence={})",
        kind, channel_id, can_id, dlc, origin, timestamp_source, error_flags, hardware_timestamp_ns,
        hardware_sequence);
}

void to_json(nlohmann::json& j, const CanMessage& message) {
    j = nlohmann::json{
        {"kind", message.kind},
        {"channel_id", message.channel_id},
        {"can_id", message.can_id},
        {"extended", message.extended},
        {"dlc", message.dlc},
        {"hardware_timestamp_ns", message.hardware_timestamp_ns},
        {"hardware_sequence", message.hardware_sequence},
        {"idle_duration_ns", message.idle_duration_ns},
        {"timestamp_source", message.timestamp_source},
        {"origin", message.origin},
        {"error_flags", message.error_flags},
        {"frame_flags", message.frame_flags},
        {"data", message.data},
    };
}

void from_json(const nlohmann::json& j, CanMessage& message) noexcept {
    if (!j.is_object()) {
        return;
    }
    if (const auto it = j.find("kind"); it != j.end()) {
        from_json(*it, message.kind);
    }
    core::detail::read_unsigned(j, "channel_id", message.channel_id);
    core::detail::read_unsigned(j, "can_id", message.can_id);
    core::detail::read_bool(j, "extended", message.extended);
    core::detail::read_unsigned(j, "dlc", message.dlc);
    core::detail::read_unsigned(j, "hardware_timestamp_ns", message.hardware_timestamp_ns);
    core::detail::read_unsigned(j, "hardware_sequence", message.hardware_sequence);
    core::detail::read_unsigned(j, "idle_duration_ns", message.idle_duration_ns);
    if (const auto it = j.find("timestamp_source"); it != j.end()) {
        from_json(*it, message.timestamp_source);
    }
    if (const auto it = j.find("origin"); it != j.end()) {
        from_json(*it, message.origin);
    }
    if (const auto it = j.find("error_flags"); it != j.end()) {
        from_json(*it, message.error_flags);
    }
    if (const auto it = j.find("frame_flags"); it != j.end()) {
        from_json(*it, message.frame_flags);
    }
    if (const auto it = j.find("data"); it != j.end() && it->is_array()) {
        const std::size_t count = std::min<std::size_t>(it->size(), message.data.size());
        for (std::size_t i = 0; i < count; ++i) {
            if ((*it)[i].is_number_unsigned()) {
                const auto value = (*it)[i].get<std::uint16_t>();
                if (value <= 0xFFU) {
                    message.data[i] = static_cast<std::uint8_t>(value);
                }
            }
        }
    }
}

} // namespace nexigen::hw::can
