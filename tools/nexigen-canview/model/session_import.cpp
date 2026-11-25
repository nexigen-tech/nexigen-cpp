#include "session_import.hpp"

#include <fstream>
#include <optional>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/enum_traits.hpp>

namespace nexigen::canview {

namespace {

bool has_channel(const CanViewSessionHeader& header, std::uint16_t channel_id) noexcept {
    for (const auto& channel : header.channels) {
        if (channel.channel_id == channel_id) {
            return true;
        }
    }
    return false;
}

void ensure_channel(CanViewSessionHeader& header, std::uint16_t channel_id, std::string_view name) {
    if (has_channel(header, channel_id)) {
        return;
    }
    CanViewChannel channel{};
    channel.channel_id = channel_id;
    channel.name = name.empty() ? "legacy-" + std::to_string(channel_id) : std::string(name);
    header.channels.push_back(std::move(channel));
}

void parse_legacy_data(const nlohmann::json& value, nexigen::hw::can::CanMessage& message) {
    if (value.is_string()) {
        static_cast<void>(payload_from_hex(value.get_ref<const std::string&>(), message));
        return;
    }
    if (!value.is_array()) {
        return;
    }
    std::size_t count = 0U;
    for (const auto& entry : value) {
        if (!entry.is_number_unsigned() || count >= message.data.size()) {
            continue;
        }
        const auto byte = entry.get<std::uint16_t>();
        if (byte <= 0xFFU) {
            message.data[count++] = static_cast<std::uint8_t>(byte);
        }
    }
    message.dlc = static_cast<std::uint8_t>(count);
}

bool is_legacy_frame(const nlohmann::json& object) noexcept {
    return object.contains("hardware_timestamp_ns") ||
           (object.contains("can_id") && object.contains("data"));
}

bool is_legacy_status(const nlohmann::json& object) noexcept {
    return object.contains("state") || object.contains("dropped_count") ||
           object.contains("rx_error_counter");
}

void parse_legacy_frame(const nlohmann::json& object, SessionRecord& record) {
    record.type = CanViewRecordType::Frame;
    from_json(object, record.frame);
    if (record.frame.hw_ts_ns == 0U) {
        static_cast<void>(
            core::detail::read_unsigned(object, "hardware_timestamp_ns", record.frame.hw_ts_ns));
    }
    if (record.frame.message.hardware_timestamp_ns == 0U) {
        record.frame.message.hardware_timestamp_ns = record.frame.hw_ts_ns;
    }
    if (!object.contains("channel")) {
        static_cast<void>(
            core::detail::read_unsigned(object, "channel_id", record.frame.message.channel_id));
    }
    if (const auto it = object.find("data"); it != object.end()) {
        parse_legacy_data(*it, record.frame.message);
    }
}

void parse_legacy_status(const nlohmann::json& object, SessionRecord& record) {
    record.type = CanViewRecordType::Status;
    from_json(object, record.status);
    nexigen::hw::can::from_json(object, record.status.status);
    if (!object.contains("channel")) {
        static_cast<void>(
            core::detail::read_unsigned(object, "channel_id", record.status.channel_id));
    }
}

bool parse_record_object(const nlohmann::json& object, SessionRecord& record) {
    const std::string type = object.value("type", std::string{});
    if (type == "frame") {
        record.type = CanViewRecordType::Frame;
        from_json(object, record.frame);
        return true;
    }
    if (type == "gap") {
        record.type = CanViewRecordType::Gap;
        from_json(object, record.gap);
        return true;
    }
    if (type == "status") {
        record.type = CanViewRecordType::Status;
        from_json(object, record.status);
        return true;
    }
    if (is_legacy_frame(object)) {
        parse_legacy_frame(object, record);
        return true;
    }
    if (is_legacy_status(object)) {
        parse_legacy_status(object, record);
        return true;
    }
    return false;
}

void apply_header(ImportResult& result, const nlohmann::json& object, bool& header_seen,
                  std::optional<std::uint64_t>& first_boot) {
    CanViewSessionHeader extra{};
    from_json(object, extra);
    if (!first_boot.has_value()) {
        first_boot = extra.boot_token;
    } else if (*first_boot != extra.boot_token) {
        result.mixed_boot = true;
    }
    if (!header_seen) {
        result.header = std::move(extra);
        header_seen = true;
    }
}

void assign_local(ImportResult& result, SessionRecord& record, std::uint64_t& local_seq) {
    ++local_seq;
    if (record.type == CanViewRecordType::Frame) {
        record.frame.seq = local_seq;
        ensure_channel(result.header, record.frame.message.channel_id, record.frame.channel_name);
    } else if (record.type == CanViewRecordType::Gap) {
        record.gap.seq = local_seq;
        ensure_channel(result.header, record.gap.channel_id, record.gap.channel_name);
    } else {
        record.status.seq = local_seq;
        ensure_channel(result.header, record.status.channel_id, record.status.channel_name);
        result.statuses.push_back(record.status);
    }
}

bool looks_like_csv(std::string_view raw) {
    return raw.find(',') != std::string_view::npos && raw.find("seq") != std::string_view::npos &&
           !nlohmann::json::accept(raw);
}

bool process_document(ImportResult& result, const nlohmann::json& object, bool& header_seen,
                      std::optional<std::uint64_t>& first_boot, std::uint64_t& local_seq,
                      std::size_t max_records, bool& first_json) {
    if (first_json) {
        first_json = false;
        if (object.value("type", std::string{}) == "session") {
            apply_header(result, object, header_seen, first_boot);
            return true;
        }
        result.legacy = true;
    }
    if (object.value("type", std::string{}) == "session") {
        apply_header(result, object, header_seen, first_boot);
        return true;
    }
    if (result.records.size() >= max_records) {
        result.truncated = true;
        ++result.skipped;
        return false;
    }
    SessionRecord record{};
    if (!parse_record_object(object, record)) {
        ++result.malformed;
        return true;
    }
    assign_local(result, record, local_seq);
    result.records.push_back(std::move(record));
    return true;
}

} // namespace

const SessionStatusRecord* SessionImporter::status_for(const ImportResult& result,
                                                       std::uint16_t channel_id) noexcept {
    const SessionStatusRecord* found = nullptr;
    for (const auto& status : result.statuses) {
        if (status.channel_id == channel_id) {
            found = &status;
        }
    }
    return found;
}

ImportResult SessionImporter::parse(std::string_view content, std::size_t max_line,
                                    std::size_t max_records, const std::atomic<bool>* cancelled) {
    ImportResult result{};
    result.header.format = std::string(CanViewSessionHeader::format_name);
    result.header.version = CanViewSessionHeader::format_version;
    bool header_seen = false;
    bool first_json = true;
    std::optional<std::uint64_t> first_boot;
    std::uint64_t local_seq = 0U;
    std::size_t position = 0U;
    bool last_line_incomplete = false;

    while (position <= content.size()) {
        const std::size_t end = content.find('\n', position);
        const std::string_view raw = content.substr(
            position, end == std::string_view::npos ? content.size() - position : end - position);
        last_line_incomplete = end == std::string_view::npos;
        position = end == std::string_view::npos ? content.size() + 1U : end + 1U;
        ++result.lines;
        if (raw.empty()) {
            continue;
        }
        if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            break;
        }
        if (looks_like_csv(raw)) {
            result.unsupported_format = true;
            ++result.malformed;
            break;
        }
        if (raw.size() > max_line) {
            ++result.malformed;
            continue;
        }
        if (!nlohmann::json::accept(raw)) {
            if (last_line_incomplete) {
                result.partial_tail = true;
            } else {
                ++result.malformed;
            }
            continue;
        }
        const nlohmann::json object = nlohmann::json::parse(raw);
        if (!object.is_object()) {
            ++result.malformed;
            continue;
        }
        if (!process_document(result, object, header_seen, first_boot, local_seq, max_records,
                              first_json)) {
            break;
        }
    }
    if (first_boot.has_value()) {
        result.header.boot_token = *first_boot;
    }
    return result;
}

ImportResult SessionImporter::load(const std::string& path, std::size_t max_line,
                                   std::size_t max_records, const std::atomic<bool>* cancelled) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ImportResult result{};
        ++result.malformed;
        return result;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        ImportResult result{};
        ++result.malformed;
        return result;
    }
    file.seekg(0, std::ios::beg);
    std::string content(static_cast<std::size_t>(size), '\0');
    if (!file.read(content.data(), size)) {
        ImportResult result{};
        ++result.malformed;
        return result;
    }
    return parse(content, max_line, max_records, cancelled);
}

} // namespace nexigen::canview
