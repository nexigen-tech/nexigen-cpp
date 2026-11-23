#include <nexigen/hw/can/can_error_flags.hpp>

namespace nexigen::hw::can {
namespace {

struct NamedFlag {
    std::uint32_t flag;
    std::string_view name;
};

constexpr NamedFlag named_flags[] = {
    {CanErrorFlags::bit_error, "bit_error"},
    {CanErrorFlags::stuff_error, "stuff_error"},
    {CanErrorFlags::crc_error, "crc_error"},
    {CanErrorFlags::form_error, "form_error"},
    {CanErrorFlags::ack_error, "ack_error"},
    {CanErrorFlags::bus_off, "bus_off"},
    {CanErrorFlags::error_passive, "error_passive"},
    {CanErrorFlags::error_warning, "error_warning"},
    {CanErrorFlags::arbitration_lost, "arbitration_lost"},
    {CanErrorFlags::tx_timeout, "tx_timeout"},
    {CanErrorFlags::controller_restart, "controller_restart"},
    {CanErrorFlags::overrun, "overrun"},
    {CanErrorFlags::other, "other"},
};

} // namespace

std::string CanErrorFlags::to_string() const {
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
