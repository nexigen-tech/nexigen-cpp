#include <nexigen/hw/can/can_channel_set.hpp>

#include <cstddef>

namespace nexigen::hw::can {

core::Status validate_channels(std::span<const CanChannelConfig> channels) noexcept {
    constexpr std::size_t max_channels = 16U;
    if (channels.size() > max_channels) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    for (std::size_t i = 0; i < channels.size(); ++i) {
        if (core::Status valid = channels[i].validate(); !valid) {
            return valid;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (channels[i].name == channels[j].name) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            if (channels[i].channel_id == channels[j].channel_id) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }
    }
    return {};
}

} // namespace nexigen::hw::can
