#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::hw::can {

std::string CanChannelStatus::to_string() const {
    return std::format(
        "CanChannelStatus(state={}, tx_errors={}, rx_errors={}, bus_off={}, errors={}, rx={}, "
        "tx={}, dropped={}, gaps={}, last_error_flags={}, timestamp_ns={})",
        state, tx_error_counter, rx_error_counter, bus_off_count, error_frame_count, rx_count,
        tx_count, dropped_count, sequence_gap_count, last_error_flags, timestamp_ns);
}

void to_json(nlohmann::json& j, const CanChannelStatus& status) {
    j = nlohmann::json{
        {"state", status.state},
        {"tx_error_counter", status.tx_error_counter},
        {"rx_error_counter", status.rx_error_counter},
        {"bus_off_count", status.bus_off_count},
        {"error_frame_count", status.error_frame_count},
        {"rx_count", status.rx_count},
        {"tx_count", status.tx_count},
        {"dropped_count", status.dropped_count},
        {"sequence_gap_count", status.sequence_gap_count},
        {"last_error_flags", status.last_error_flags},
        {"timestamp_ns", status.timestamp_ns},
    };
}

void from_json(const nlohmann::json& j, CanChannelStatus& status) noexcept {
    if (!j.is_object()) {
        return;
    }
    if (const auto it = j.find("state"); it != j.end()) {
        from_json(*it, status.state);
    }
    core::detail::read_unsigned(j, "tx_error_counter", status.tx_error_counter);
    core::detail::read_unsigned(j, "rx_error_counter", status.rx_error_counter);
    core::detail::read_unsigned(j, "bus_off_count", status.bus_off_count);
    core::detail::read_unsigned(j, "error_frame_count", status.error_frame_count);
    core::detail::read_unsigned(j, "rx_count", status.rx_count);
    core::detail::read_unsigned(j, "tx_count", status.tx_count);
    core::detail::read_unsigned(j, "dropped_count", status.dropped_count);
    core::detail::read_unsigned(j, "sequence_gap_count", status.sequence_gap_count);
    if (const auto it = j.find("last_error_flags"); it != j.end()) {
        from_json(*it, status.last_error_flags);
    }
    core::detail::read_unsigned(j, "timestamp_ns", status.timestamp_ns);
}

} // namespace nexigen::hw::can
