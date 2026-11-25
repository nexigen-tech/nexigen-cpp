#include "view_channel.hpp"

#include <format>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::canview {

std::string CanViewChannel::to_string() const {
    return std::format("CanViewChannel(name={}, id={}, nominal={}, data={}, fd={}, enabled={})",
                       name, channel_id, nominal_bitrate, data_bitrate, fd, enabled);
}

void to_json(nlohmann::json& j, const CanViewChannel& channel) {
    j = nlohmann::json{{"name", channel.name},
                       {"channel_id", channel.channel_id},
                       {"nominal_bitrate", channel.nominal_bitrate},
                       {"data_bitrate", channel.data_bitrate},
                       {"fd", channel.fd},
                       {"enabled", channel.enabled}};
}

void from_json(const nlohmann::json& j, CanViewChannel& channel) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_string(j, "name", channel.name));
    static_cast<void>(core::detail::read_unsigned(j, "channel_id", channel.channel_id));
    static_cast<void>(core::detail::read_unsigned(j, "nominal_bitrate", channel.nominal_bitrate));
    static_cast<void>(core::detail::read_unsigned(j, "data_bitrate", channel.data_bitrate));
    static_cast<void>(core::detail::read_bool(j, "fd", channel.fd));
    static_cast<void>(core::detail::read_bool(j, "enabled", channel.enabled));
}

} // namespace nexigen::canview
