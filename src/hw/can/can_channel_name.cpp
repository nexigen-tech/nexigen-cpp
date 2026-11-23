#include <nexigen/hw/can/can_channel_name.hpp>

namespace nexigen::hw::can {

bool is_valid_channel_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 32U) {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

core::Status validate_channel_name(std::string_view name) noexcept {
    if (!is_valid_channel_name(name)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

} // namespace nexigen::hw::can
