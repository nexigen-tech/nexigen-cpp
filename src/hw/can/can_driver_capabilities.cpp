#include <nexigen/hw/can/can_driver_capabilities.hpp>
#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::hw::can {

std::string CanDriverCapabilities::to_string() const {
    return std::format(
        "CanDriverCapabilities(fd={}, remote={}, hardware_timestamps={}, hardware_sequence={}, "
        "error_frames={}, overload_frames={}, heartbeats={}, batch={}, max_channels={})",
        supports_fd, supports_remote, hardware_timestamps, hardware_sequence, error_frames,
        overload_frames, heartbeats, batch_submission, max_channels);
}

void to_json(nlohmann::json& j, const CanDriverCapabilities& capabilities) {
    j = nlohmann::json{
        {"supports_fd", capabilities.supports_fd},
        {"supports_remote", capabilities.supports_remote},
        {"hardware_timestamps", capabilities.hardware_timestamps},
        {"hardware_sequence", capabilities.hardware_sequence},
        {"error_frames", capabilities.error_frames},
        {"overload_frames", capabilities.overload_frames},
        {"heartbeats", capabilities.heartbeats},
        {"batch_submission", capabilities.batch_submission},
        {"max_channels", capabilities.max_channels},
    };
}

void from_json(const nlohmann::json& j, CanDriverCapabilities& capabilities) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_bool(j, "supports_fd", capabilities.supports_fd);
    core::detail::read_bool(j, "supports_remote", capabilities.supports_remote);
    core::detail::read_bool(j, "hardware_timestamps", capabilities.hardware_timestamps);
    core::detail::read_bool(j, "hardware_sequence", capabilities.hardware_sequence);
    core::detail::read_bool(j, "error_frames", capabilities.error_frames);
    core::detail::read_bool(j, "overload_frames", capabilities.overload_frames);
    core::detail::read_bool(j, "heartbeats", capabilities.heartbeats);
    core::detail::read_bool(j, "batch_submission", capabilities.batch_submission);
    if (const auto it = j.find("max_channels"); it != j.end() && it->is_number_unsigned()) {
        capabilities.max_channels = static_cast<std::uint16_t>(it->get<std::uint32_t>());
    }
}

} // namespace nexigen::hw::can
