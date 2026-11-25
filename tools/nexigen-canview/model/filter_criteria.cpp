#include "filter_criteria.hpp"

#include <format>

#include <nexigen/core/detail/json_field.hpp>

#include "record_meta.hpp"

namespace nexigen::canview {

namespace {

namespace hwcan = nexigen::hw::can;

char ascii_lower(char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

bool is_printable(unsigned char value) noexcept {
    return value >= 0x20U && value <= 0x7EU;
}

int hex_value(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool is_separator(char value) noexcept {
    return value == ' ' || value == '\t' || value == ':' || value == '-' || value == ',';
}

bool parse_hex(std::string_view text,
               std::array<std::uint8_t, FilterCriteria::max_query_bytes>& out,
               std::uint8_t& length) noexcept {
    std::size_t count = 0;
    int high = -1;
    for (const char character : text) {
        if (is_separator(character)) {
            continue;
        }
        const int value = hex_value(character);
        if (value < 0) {
            return false;
        }
        if (high < 0) {
            high = value;
            continue;
        }
        if (count >= out.size()) {
            return false;
        }
        out[count] = static_cast<std::uint8_t>((high << 4) | value);
        ++count;
        high = -1;
    }
    if (high >= 0 || count == 0U) {
        return false;
    }
    length = static_cast<std::uint8_t>(count);
    return true;
}

bool contains_ci(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    for (std::size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        bool equal = true;
        for (std::size_t offset = 0; offset < needle.size(); ++offset) {
            if (ascii_lower(haystack[start + offset]) != ascii_lower(needle[offset])) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return true;
        }
    }
    return false;
}

bool data_text_contains(const hwcan::CanMessage& message, std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > message.dlc) {
        return false;
    }
    for (std::size_t start = 0; start + needle.size() <= message.dlc; ++start) {
        bool equal = true;
        for (std::size_t offset = 0; offset < needle.size(); ++offset) {
            const unsigned char value = message.data[start + offset];
            if (!is_printable(value) ||
                ascii_lower(static_cast<char>(value)) != ascii_lower(needle[offset])) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return true;
        }
    }
    return false;
}

} // namespace

void FilterCriteria::set_text(std::string value) {
    text = std::move(value);
    hex_ = {};
    hex_length_ = 0;
    hex_mode_ = false;
    if (!text.empty()) {
        hex_mode_ = parse_hex(text, hex_, hex_length_);
    }
}

void FilterCriteria::set_node_visible(std::uint16_t slot, bool visible) noexcept {
    if (slot >= 128U) {
        return;
    }
    const std::uint64_t mask = 1ULL << (slot % 64U);
    if (visible) {
        node_mask[slot / 64U] |= mask;
    } else {
        node_mask[slot / 64U] &= ~mask;
    }
}

bool FilterCriteria::is_error_like(const CanRecordView& record) noexcept {
    if (record.message == nullptr) {
        return false;
    }
    return record.message->kind == hwcan::CanMessageKind::Error ||
           record.message->kind == hwcan::CanMessageKind::Overload ||
           has_flag(record.meta, RecordFlags::InvalidStruct);
}

bool FilterCriteria::text_matches(const CanRecordView& record,
                                  const FilterLabels& labels) const noexcept {
    if (text.empty()) {
        return true;
    }
    if (hex_mode_) {
        if (record.message == nullptr || hex_length_ > record.message->dlc) {
            return false;
        }
        for (std::size_t start = 0; start + hex_length_ <= record.message->dlc; ++start) {
            bool equal = true;
            for (std::size_t offset = 0; offset < hex_length_; ++offset) {
                if (record.message->data[start + offset] != hex_[offset]) {
                    equal = false;
                    break;
                }
            }
            if (equal) {
                return true;
            }
        }
        return false;
    }
    if (record.message != nullptr && data_text_contains(*record.message, text)) {
        return true;
    }
    if (contains_ci(labels.channel_alias, text)) {
        return true;
    }
    if (contains_ci(labels.node_alias, text)) {
        return true;
    }
    if (!record.is_frame()) {
        return contains_ci(nexigen::canview::to_string(meta_gap_reason(record.meta)), text);
    }
    return false;
}

bool FilterCriteria::matches_loss(const CanRecordView& record,
                                  const FilterLabels& labels) const noexcept {
    // Loss records bypass ID/node/kind/origin so an identifier filter can
    // never hide the gap that explains its missing frames.
    if (!show_gaps && !errors_only) {
        return false;
    }
    if (!channel_visible(record.message->channel_id)) {
        return false;
    }
    return text_matches(record, labels);
}

bool FilterCriteria::matches_bus_idle(const CanRecordView& record,
                                      const FilterLabels& labels) const noexcept {
    if (!show_bus_idle || (kind_mask & (1U << static_cast<std::uint8_t>(record.kind()))) == 0U) {
        return false;
    }
    if (!channel_visible(record.message->channel_id)) {
        return false;
    }
    return text_matches(record, labels);
}

bool FilterCriteria::matches_frame(const CanRecordView& record,
                                   const FilterLabels& labels) const noexcept {
    const hwcan::CanMessage& message = *record.message;
    if (!channel_visible(message.channel_id)) {
        return false;
    }
    if (errors_only && !is_error_like(record)) {
        return false;
    }
    const auto kind_bit = static_cast<std::uint8_t>(record.kind());
    if ((kind_mask & (1U << kind_bit)) == 0U) {
        return false;
    }
    const auto origin_bit = static_cast<std::uint8_t>(message.origin);
    if ((origin_mask & (1U << origin_bit)) == 0U) {
        return false;
    }
    if (id_filter_active && ((message.can_id & id_mask) != (id & id_mask))) {
        return false;
    }
    if (node_filter_active && !node_visible(record.co_node_index)) {
        return false;
    }
    return text_matches(record, labels);
}

bool FilterCriteria::matches(const CanRecordView& record,
                             const FilterLabels& labels) const noexcept {
    if (record.message == nullptr) {
        return false;
    }
    const CanRecordKind kind = record.kind();
    if (is_loss(kind)) {
        return matches_loss(record, labels);
    }
    if (kind == CanRecordKind::BusIdleHeartbeat) {
        return matches_bus_idle(record, labels);
    }
    return matches_frame(record, labels);
}

bool FilterCriteria::matches(const RecordSource& source, std::uint64_t seq,
                             const FilterLabels& labels) const noexcept {
    const auto record = source.find(seq);
    return record.has_value() && matches(*record, labels);
}

std::string FilterCriteria::to_string() const {
    return std::format(
        "FilterCriteria(channels=0x{:08x}, id=0x{:x}, mask=0x{:x}, id_active={}, nodes={}, "
        "kinds=0x{:02x}, origins=0x{:02x}, errors={}, bus_idle={}, gaps={}, text='{}')",
        channel_mask, id, id_mask, id_filter_active, node_filter_active, kind_mask, origin_mask,
        errors_only, show_bus_idle, show_gaps, text);
}

void to_json(nlohmann::json& j, const FilterCriteria& criteria) {
    j = nlohmann::json{{"channel_mask", criteria.channel_mask},
                       {"id", criteria.id},
                       {"id_mask", criteria.id_mask},
                       {"id_filter_active", criteria.id_filter_active},
                       {"node_mask", criteria.node_mask},
                       {"node_filter_active", criteria.node_filter_active},
                       {"kind_mask", criteria.kind_mask},
                       {"origin_mask", criteria.origin_mask},
                       {"errors_only", criteria.errors_only},
                       {"show_bus_idle", criteria.show_bus_idle},
                       {"show_gaps", criteria.show_gaps},
                       {"text", criteria.text}};
}

void from_json(const nlohmann::json& j, FilterCriteria& criteria) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "channel_mask", criteria.channel_mask));
    static_cast<void>(core::detail::read_unsigned(j, "id", criteria.id));
    static_cast<void>(core::detail::read_unsigned(j, "id_mask", criteria.id_mask));
    static_cast<void>(core::detail::read_bool(j, "id_filter_active", criteria.id_filter_active));
    static_cast<void>(
        core::detail::read_bool(j, "node_filter_active", criteria.node_filter_active));
    static_cast<void>(core::detail::read_unsigned(j, "kind_mask", criteria.kind_mask));
    static_cast<void>(core::detail::read_unsigned(j, "origin_mask", criteria.origin_mask));
    static_cast<void>(core::detail::read_bool(j, "errors_only", criteria.errors_only));
    static_cast<void>(core::detail::read_bool(j, "show_bus_idle", criteria.show_bus_idle));
    static_cast<void>(core::detail::read_bool(j, "show_gaps", criteria.show_gaps));
    if (const auto it = j.find("node_mask"); it != j.end() && it->is_array() && it->size() == 2U) {
        for (std::size_t index = 0; index < 2U; ++index) {
            if ((*it)[index].is_number_unsigned()) {
                criteria.node_mask[index] = (*it)[index].get<std::uint64_t>();
            }
        }
    }
    std::string text = criteria.text;
    if (core::detail::read_string(j, "text", text)) {
        criteria.set_text(std::move(text));
    }
}

} // namespace nexigen::canview
