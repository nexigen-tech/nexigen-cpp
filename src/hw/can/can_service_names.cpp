#include <nexigen/hw/can/can_service_names.hpp>

#include <string>

#include <nexigen/hw/can/can_channel_name.hpp>

namespace nexigen::hw::can {
namespace {

core::Result<std::string> build(std::string_view channel, std::string_view suffix) noexcept {
    if (core::Status valid = validate_channel_name(channel); !valid) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    std::string name = "nexigen/hw/can/";
    name += channel;
    name += "/";
    name += suffix;
    return name;
}

} // namespace

core::Result<std::string> rx_service(std::string_view channel) noexcept {
    return build(channel, "rx");
}

core::Result<std::string> tx_service(std::string_view channel) noexcept {
    return build(channel, "tx");
}

core::Result<std::string> status_service(std::string_view channel) noexcept {
    return build(channel, "status");
}

core::Result<std::string> vbus_rx_service(std::string_view channel) noexcept {
    return build(channel, "vbus/rx");
}

core::Result<std::string> vbus_tx_service(std::string_view channel) noexcept {
    return build(channel, "vbus/tx");
}

} // namespace nexigen::hw::can
