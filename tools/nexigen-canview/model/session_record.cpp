#include "session_record.hpp"

#include <cstdio>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/enum_traits.hpp>

namespace nexigen::canview {

namespace {

std::string enum_text(auto value) {
    return std::string(to_string(value));
}

} // namespace

std::string payload_to_hex(const nexigen::hw::can::CanMessage& message) {
    static constexpr char digits[] = "0123456789abcdef";
    const std::size_t bytes = message.dlc < message.data.size() ? message.dlc : message.data.size();
    std::string out;
    out.reserve(bytes * 2U);
    for (std::size_t index = 0; index < bytes; ++index) {
        out.push_back(digits[(message.data[index] >> 4U) & 0x0FU]);
        out.push_back(digits[message.data[index] & 0x0FU]);
    }
    return out;
}

bool payload_from_hex(std::string_view text, nexigen::hw::can::CanMessage& message) noexcept {
    std::size_t count = 0U;
    int high = -1;
    for (const char character : text) {
        if (character == ' ' || character == '\t' || character == ':') {
            continue;
        }
        int value = -1;
        if (character >= '0' && character <= '9') {
            value = character - '0';
        } else if (character >= 'a' && character <= 'f') {
            value = character - 'a' + 10;
        } else if (character >= 'A' && character <= 'F') {
            value = character - 'A' + 10;
        } else {
            return count > 0U;
        }
        if (high < 0) {
            high = value;
            continue;
        }
        if (count >= message.data.size()) {
            break;
        }
        message.data[count++] = static_cast<std::uint8_t>((high << 4) | value);
        high = -1;
    }
    message.dlc = static_cast<std::uint8_t>(count);
    return count > 0U;
}

void to_json(nlohmann::json& j, const SessionEdsBinding& binding) {
    j = nlohmann::json{{"channel_id", binding.channel_id},
                       {"node_id", binding.node_id},
                       {"alias", binding.alias},
                       {"path", binding.path},
                       {"hash", binding.hash},
                       {"colour", binding.colour}};
}

void from_json(const nlohmann::json& j, SessionEdsBinding& binding) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "channel_id", binding.channel_id));
    static_cast<void>(core::detail::read_unsigned(j, "node_id", binding.node_id));
    static_cast<void>(core::detail::read_string(j, "alias", binding.alias));
    static_cast<void>(core::detail::read_string(j, "path", binding.path));
    static_cast<void>(core::detail::read_unsigned(j, "hash", binding.hash));
    static_cast<void>(core::detail::read_unsigned(j, "colour", binding.colour));
}

void to_json(nlohmann::json& j, const CanViewSessionHeader& header) {
    nlohmann::json channels = nlohmann::json::array();
    for (const auto& channel : header.channels) {
        channels.push_back(channel);
    }
    nlohmann::json eds = nlohmann::json::array();
    for (const auto& binding : header.eds) {
        eds.push_back(nlohmann::json{{"channel_id", binding.channel_id},
                                     {"node_id", binding.node_id},
                                     {"alias", binding.alias},
                                     {"path", binding.path},
                                     {"hash", binding.hash},
                                     {"colour", binding.colour}});
    }
    j = nlohmann::json{{"type", "session"},
                       {"format", header.format},
                       {"version", header.version},
                       {"created_ns", header.created_ns},
                       {"boot_token", header.boot_token},
                       {"timebase", header.timebase},
                       {"channels", std::move(channels)},
                       {"eds", std::move(eds)},
                       {"counters", {{"frames", header.frame_count}, {"gaps", header.gap_count}}}};
}

void from_json(const nlohmann::json& j, CanViewSessionHeader& header) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_string(j, "format", header.format));
    static_cast<void>(core::detail::read_unsigned(j, "version", header.version));
    static_cast<void>(core::detail::read_signed(j, "created_ns", header.created_ns));
    static_cast<void>(core::detail::read_unsigned(j, "boot_token", header.boot_token));
    static_cast<void>(core::detail::read_string(j, "timebase", header.timebase));
    if (const auto it = j.find("channels"); it != j.end() && it->is_array()) {
        header.channels.clear();
        for (const auto& entry : *it) {
            CanViewChannel channel{};
            from_json(entry, channel);
            header.channels.push_back(std::move(channel));
        }
    }
    if (const auto it = j.find("eds"); it != j.end() && it->is_array()) {
        header.eds.clear();
        for (const auto& entry : *it) {
            SessionEdsBinding binding{};
            static_cast<void>(core::detail::read_unsigned(entry, "channel_id", binding.channel_id));
            static_cast<void>(core::detail::read_unsigned(entry, "node_id", binding.node_id));
            static_cast<void>(core::detail::read_string(entry, "alias", binding.alias));
            static_cast<void>(core::detail::read_string(entry, "path", binding.path));
            static_cast<void>(core::detail::read_unsigned(entry, "hash", binding.hash));
            static_cast<void>(core::detail::read_unsigned(entry, "colour", binding.colour));
            header.eds.push_back(std::move(binding));
        }
    }
    if (const auto it = j.find("counters"); it != j.end() && it->is_object()) {
        static_cast<void>(core::detail::read_unsigned(*it, "frames", header.frame_count));
        static_cast<void>(core::detail::read_unsigned(*it, "gaps", header.gap_count));
    }
}

void to_json(nlohmann::json& j, const SessionFrameRecord& record) {
    const nexigen::hw::can::CanMessage& message = record.message;
    j = nlohmann::json{
        {"type", "frame"},
        {"seq", record.seq},
        {"hw_ts_ns", record.hw_ts_ns},
        {"delta_ns", record.delta_ns},
        {"delta_id_ns", record.delta_id_ns},
        {"channel", message.channel_id},
        {"channel_name", record.channel_name},
        {"can_id", message.can_id},
        {"extended", message.extended},
        {"kind", message.kind},
        {"origin", message.origin},
        {"ts_source", message.timestamp_source},
        {"hw_seq", message.hardware_sequence},
        {"dlc", message.dlc},
        {"brs", message.frame_flags.has(nexigen::hw::can::CanFrameFlags::bit_rate_switch)},
        {"esi", message.frame_flags.has(nexigen::hw::can::CanFrameFlags::error_state_indicator)},
        {"error_flags", message.error_flags.bits},
        {"frame_flags", message.frame_flags.bits},
        {"idle_ns", message.idle_duration_ns},
        {"data", payload_to_hex(message)},
        {"co_decoded", record.co_decoded}};
    if (record.utc_est_ns.has_value()) {
        j["utc_est_ns"] = *record.utc_est_ns;
        j["utc_estimated"] = true;
        j["utc_source"] = record.utc_source;
    }
}

void from_json(const nlohmann::json& j, SessionFrameRecord& record) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "seq", record.seq));
    static_cast<void>(core::detail::read_unsigned(j, "hw_ts_ns", record.hw_ts_ns));
    static_cast<void>(core::detail::read_signed(j, "delta_ns", record.delta_ns));
    static_cast<void>(core::detail::read_signed(j, "delta_id_ns", record.delta_id_ns));
    static_cast<void>(core::detail::read_string(j, "channel_name", record.channel_name));
    static_cast<void>(core::detail::read_string(j, "co_decoded", record.co_decoded));
    if (const auto it = j.find("utc_est_ns"); it != j.end() && it->is_number_integer()) {
        record.utc_est_ns = it->get<std::int64_t>();
    }
    static_cast<void>(core::detail::read_string(j, "utc_source", record.utc_source));

    nexigen::hw::can::CanMessage& message = record.message;
    static_cast<void>(core::detail::read_unsigned(j, "channel", message.channel_id));
    static_cast<void>(core::detail::read_unsigned(j, "can_id", message.can_id));
    static_cast<void>(core::detail::read_bool(j, "extended", message.extended));
    static_cast<void>(core::detail::read_unsigned(j, "dlc", message.dlc));
    static_cast<void>(core::detail::read_unsigned(j, "hw_seq", message.hardware_sequence));
    static_cast<void>(core::detail::read_unsigned(j, "idle_ns", message.idle_duration_ns));
    if (const auto it = j.find("kind"); it != j.end()) {
        from_json(*it, message.kind);
    }
    if (const auto it = j.find("origin"); it != j.end()) {
        from_json(*it, message.origin);
    }
    if (const auto it = j.find("ts_source"); it != j.end()) {
        from_json(*it, message.timestamp_source);
    }
    if (const auto it = j.find("frame_flags"); it != j.end() && it->is_number_unsigned()) {
        message.frame_flags.bits = it->get<std::uint16_t>();
    }
    if (const auto it = j.find("error_flags"); it != j.end() && it->is_number_unsigned()) {
        message.error_flags.bits = it->get<std::uint32_t>();
    }
    if (const auto it = j.find("data"); it != j.end() && it->is_string()) {
        static_cast<void>(payload_from_hex(it->get_ref<const std::string&>(), message));
    }
    if (message.hardware_timestamp_ns == 0U) {
        message.hardware_timestamp_ns = record.hw_ts_ns;
    }
}

void to_json(nlohmann::json& j, const SessionGapRecord& record) {
    j = nlohmann::json{{"type", "gap"},
                       {"seq", record.seq},
                       {"kind", record.kind},
                       {"reason", record.reason},
                       {"lost", record.lost},
                       {"count_exact", record.count_exact},
                       {"hw_ts_ns", record.hw_ts_ns},
                       {"channel", record.channel_id},
                       {"channel_name", record.channel_name},
                       {"time_epoch", record.time_epoch}};
}

void from_json(const nlohmann::json& j, SessionGapRecord& record) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "seq", record.seq));
    static_cast<void>(core::detail::read_unsigned(j, "lost", record.lost));
    static_cast<void>(core::detail::read_bool(j, "count_exact", record.count_exact));
    static_cast<void>(core::detail::read_unsigned(j, "hw_ts_ns", record.hw_ts_ns));
    static_cast<void>(core::detail::read_unsigned(j, "channel", record.channel_id));
    static_cast<void>(core::detail::read_string(j, "channel_name", record.channel_name));
    static_cast<void>(core::detail::read_unsigned(j, "time_epoch", record.time_epoch));
    core::mandate::read_enum(j, "kind", record.kind);
    core::mandate::read_enum(j, "reason", record.reason);
}

void to_json(nlohmann::json& j, const SessionStatusRecord& record) {
    j = nlohmann::json{{"type", "status"},
                       {"seq", record.seq},
                       {"channel", record.channel_id},
                       {"channel_name", record.channel_name},
                       {"status", record.status}};
}

void from_json(const nlohmann::json& j, SessionStatusRecord& record) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "seq", record.seq));
    static_cast<void>(core::detail::read_unsigned(j, "channel", record.channel_id));
    static_cast<void>(core::detail::read_string(j, "channel_name", record.channel_name));
    if (const auto it = j.find("status"); it != j.end()) {
        from_json(*it, record.status);
    }
}

} // namespace nexigen::canview
