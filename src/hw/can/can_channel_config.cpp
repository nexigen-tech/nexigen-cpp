#include <nexigen/hw/can/can_channel_config.hpp>
#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::hw::can {

core::Status CanChannelConfig::validate() const noexcept {
    if (core::Status name_status = validate_channel_name(name); !name_status) {
        return name_status;
    }
    if (driver_channel.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (bit_rate == 0U || rx_ring == 0U || tx_queue == 0U) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    constexpr std::size_t max_capacity = 1U << 20U;
    if (rx_ring > max_capacity || tx_queue > max_capacity) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

std::string CanChannelConfig::to_string() const {
    return std::format(
        "CanChannelConfig(name={}, id={}, driver={}, bit_rate={}, fd={}, rx_ring={}, tx_queue={})",
        name, channel_id, driver_channel, bit_rate, fd_enabled, rx_ring, tx_queue);
}

void to_json(nlohmann::json& j, const CanChannelConfig& config) {
    j = nlohmann::json{
        {"name", config.name},
        {"channel_id", config.channel_id},
        {"driver_channel", config.driver_channel},
        {"bit_rate", config.bit_rate},
        {"fd", config.fd_enabled},
        {"rx_ring", config.rx_ring},
        {"tx_queue", config.tx_queue},
        {"drop_oldest", config.drop_oldest},
        {"heartbeat_interval_ns", config.heartbeat_interval_ns},
        {"status_interval_ns", config.status_interval_ns},
    };
}

void from_json(const nlohmann::json& j, CanChannelConfig& config) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_string(j, "name", config.name);
    core::detail::read_string(j, "driver_channel", config.driver_channel);
    core::detail::read_unsigned(j, "channel_id", config.channel_id);
    core::detail::read_unsigned(j, "bit_rate", config.bit_rate);
    core::detail::read_bool(j, "fd", config.fd_enabled);
    core::detail::read_unsigned(j, "rx_ring", config.rx_ring);
    core::detail::read_unsigned(j, "tx_queue", config.tx_queue);
    core::detail::read_bool(j, "drop_oldest", config.drop_oldest);
    core::detail::read_unsigned(j, "heartbeat_interval_ns", config.heartbeat_interval_ns);
    core::detail::read_unsigned(j, "status_interval_ns", config.status_interval_ns);
}

} // namespace nexigen::hw::can
