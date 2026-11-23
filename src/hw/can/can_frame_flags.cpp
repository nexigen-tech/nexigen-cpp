#include <nexigen/hw/can/can_frame_flags.hpp>

namespace nexigen::hw::can {
namespace {

struct NamedFlag {
    std::uint16_t flag;
    std::string_view name;
};

constexpr NamedFlag named_flags[] = {
    {CanFrameFlags::bit_rate_switch, "brs"},
    {CanFrameFlags::error_state_indicator, "esi"},
    {CanFrameFlags::has_hardware_timestamp, "hardware_timestamp"},
    {CanFrameFlags::has_hardware_sequence, "hardware_sequence"},
};

} // namespace

std::string CanFrameFlags::to_string() const {
    if (bits == 0U) {
        return "none";
    }
    std::string out;
    for (const auto& entry : named_flags) {
        if (!has(entry.flag)) {
            continue;
        }
        if (!out.empty()) {
            out += ",";
        }
        out += entry.name;
    }
    return out;
}

} // namespace nexigen::hw::can
