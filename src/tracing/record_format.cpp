// nexigen::tracing — cold serialization bodies for the data-model value
// types (design D2/D4): TraceId/SpanId/SpanContext (task 3.1) and later in
// this file SpanAttribute/SpanRecord (task 3.2). Every allocating/std::string
// /nlohmann operation for the record model lives here, OFF the hot headers —
// span start/attribute/status/enqueue never include this TU's machinery.
//
// Hex contract: identifiers render as zero-padded 16-character lowercase hex
// (std::to_chars, base 16) in to_string and JSON; from_json accepts any-case
// hex of at most 16 digits and leaves the value untouched otherwise.
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/tracing/span_context.hpp>
#include <nexigen/tracing/span_record.hpp>
#include <nexigen/tracing/tracer_provider.hpp>

namespace nexigen::tracing {
namespace {

/// Render a 64-bit id as exactly 16 zero-padded lowercase hex characters.
void format_hex16(std::uint64_t value, char (&out)[16]) noexcept {
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < 16U; ++i) {
        // Most significant nibble first: nibble i occupies bits 60-4i.
        const unsigned shift = 60U - static_cast<unsigned>(i) * 4U;
        out[i] = digits[(value >> shift) & 0xFULL];
    }
}

/// Parse 1..16 hex digits into a 64-bit count; false on anything else.
[[nodiscard]] bool parse_hex16(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty() || text.size() > 16U) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        std::uint64_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint64_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint64_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<std::uint64_t>(c - 'A' + 10);
        } else {
            return false;
        }
        value = (value << 4U) | digit;
    }
    out = value;
    return true;
}

/// Stable-storage intern table for location strings restored by from_json:
/// file_name/function_name are pointers into PROGRAM-STATIC storage on the
/// producer side; deserialization cannot rebind that, so restored texts are
/// interned here (first-fit by content, capped, never allocating) and the
/// pointers stay stable for the process lifetime. Distinct texts beyond the
/// cap leave the field unchanged — a documented bound, cold-path only.
class Location_intern {
public:
    Location_intern() noexcept {
        for (std::size_t i = 0; i < slot_count_max; ++i) {
            slots_[i] = storage_.data() + i * slot_capacity;
        }
    }

    /// Returns a stable NUL-terminated copy of `text`, or nullptr when the
    /// table is full or the text does not fit a slot.
    [[nodiscard]] const char* intern(std::string_view text) noexcept {
        if (text.empty() || text.size() >= slot_capacity) {
            return nullptr;
        }
        for (std::size_t i = 0; i < used_; ++i) {
            if (std::string_view(slots_[i]) == text) {
                return slots_[i]; // already interned
            }
        }
        if (used_ >= slot_count_max) {
            return nullptr;
        }
        char* const slot = slots_[used_++];
        for (std::size_t i = 0; i < text.size(); ++i) {
            slot[i] = text[i];
        }
        slot[text.size()] = '\0';
        return slot;
    }

private:
    static constexpr std::size_t slot_capacity = 128; ///< file/function names
    static constexpr std::size_t slot_count_max = 64;
    std::array<char, slot_count_max * slot_capacity> storage_{};
    std::array<char*, slot_count_max> slots_{};
    std::size_t used_{0};
};

Location_intern& location_intern() noexcept {
    static Location_intern table{};
    return table;
}

} // namespace

// --- TraceId / SpanId -------------------------------------------------------

std::string TraceId::to_string() const {
    char hex[16];
    format_hex16(value_, hex);
    return std::string(hex, 16U);
}

std::string SpanId::to_string() const {
    char hex[16];
    format_hex16(value_, hex);
    return std::string(hex, 16U);
}

std::string to_string(const TraceId& id) { return id.to_string(); }
std::string to_string(const SpanId& id) { return id.to_string(); }

void to_json(nlohmann::json& j, const TraceId& id) {
    char hex[16];
    format_hex16(id.value(), hex);
    j = std::string(hex, 16U);
}

void from_json(const nlohmann::json& j, TraceId& out) noexcept {
    const auto* const text = j.get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return; // non-string input: value untouched (no abort under -fno-exceptions)
    }
    std::uint64_t value = 0;
    if (parse_hex16(std::string_view(*text), value)) {
        out = TraceId{value};
    }
}

void to_json(nlohmann::json& j, const SpanId& id) {
    char hex[16];
    format_hex16(id.value(), hex);
    j = std::string(hex, 16U);
}

void from_json(const nlohmann::json& j, SpanId& out) noexcept {
    const auto* const text = j.get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return;
    }
    std::uint64_t value = 0;
    if (parse_hex16(std::string_view(*text), value)) {
        out = SpanId{value};
    }
}

// --- SpanContext ------------------------------------------------------------

std::string SpanContext::to_string() const {
    return "SpanContext(trace_id=" + trace_id.to_string() +
           ", span_id=" + span_id.to_string() +
           ", sampled=" + std::string(tracing::to_string(sampled)) + ")";
}

std::string to_string(const SpanContext& context) { return context.to_string(); }

void to_json(nlohmann::json& j, const SpanContext& context) {
    j = nlohmann::json{
        {"trace_id", context.trace_id},
        {"span_id", context.span_id},
        {"sampled", context.sampled},
    };
}

void from_json(const nlohmann::json& j, SpanContext& out) noexcept {
    if (!j.is_object()) {
        return; // wrong shape: everything untouched
    }
    const auto trace = j.find("trace_id");
    if (trace != j.end()) {
        nlohmann::from_json(*trace, out.trace_id);
    }
    const auto span = j.find("span_id");
    if (span != j.end()) {
        nlohmann::from_json(*span, out.span_id);
    }
    const auto sampled = j.find("sampled");
    if (sampled != j.end()) {
        nlohmann::from_json(*sampled, out.sampled);
    }
}

// --- SpanAttribute ----------------------------------------------------------

std::string SpanAttribute::to_string() const {
    std::string out = "SpanAttribute(key=" + std::string(key_view());
    out += ", type=" + std::string(tracing::to_string(type));
    out += ", value=";
    switch (type) {
        case SpanAttributeType::Int64:  out += std::to_string(value.i64); break;
        case SpanAttributeType::Uint64: out += std::to_string(value.u64); break;
        case SpanAttributeType::Double: out += std::to_string(value.f64); break;
        case SpanAttributeType::Bool:   out += value.b ? "true" : "false"; break;
        case SpanAttributeType::String: out += '"';
                                         out += std::string(text_view());
                                         out += '"';
                                         break;
        default:                        out += "Unknown"; break;
    }
    out += ")";
    return out;
}

std::string to_string(const SpanAttribute& attribute) { return attribute.to_string(); }

void to_json(nlohmann::json& j, const SpanAttribute& attribute) {
    nlohmann::json value;
    switch (attribute.type) {
        case SpanAttributeType::Int64:  value = attribute.value.i64; break;
        case SpanAttributeType::Uint64: value = attribute.value.u64; break;
        case SpanAttributeType::Double: value = attribute.value.f64; break;
        case SpanAttributeType::Bool:   value = attribute.value.b;   break;
        case SpanAttributeType::String: value = std::string(attribute.text_view()); break;
        default:                        value = nullptr; break;
    }
    j = nlohmann::json{
        {"key", std::string(attribute.key_view())},
        {"type", attribute.type},
        {"value", value},
    };
}

void from_json(const nlohmann::json& j, SpanAttribute& out) noexcept {
    if (!j.is_object()) {
        return; // wrong shape: untouched
    }
    const auto key = j.find("key");
    if (key == j.end() || !key->is_string()) {
        return;
    }
    const auto* const key_text = key->get_ptr<const nlohmann::json::string_t*>();
    if (key_text == nullptr) {
        return;
    }
    const std::string_view key_view(*key_text);
    if (key_view.size() > attribute_key_capacity) {
        return; // oversized key: rejected whole, nothing changes
    }
    const auto type_field = j.find("type");
    if (type_field == j.end() || !type_field->is_string()) {
        return;
    }
    SpanAttributeType parsed_type = out.type;
    nlohmann::from_json(*type_field, parsed_type);
    const auto value_field = j.find("value");
    if (value_field == j.end()) {
        return;
    }
    const nlohmann::json& value = *value_field;
    // Parse into a staging attribute; commit only on a clean field parse so a
    // wrong-typed value leaves the WHOLE attribute untouched.
    SpanAttribute staged = out;
    switch (parsed_type) {
        case SpanAttributeType::Int64: {
            const auto* const number = value.get_ptr<const nlohmann::json::number_integer_t*>();
            if (number == nullptr) { return; }
            static_cast<void>(staged.store(key_view, static_cast<std::int64_t>(*number)));
            break;
        }
        case SpanAttributeType::Uint64: {
            const auto* const number = value.get_ptr<const nlohmann::json::number_unsigned_t*>();
            if (number == nullptr) { return; }
            static_cast<void>(staged.store(key_view, static_cast<std::uint64_t>(*number)));
            break;
        }
        case SpanAttributeType::Double: {
            const auto* const number = value.get_ptr<const nlohmann::json::number_float_t*>();
            if (number == nullptr) { return; }
            static_cast<void>(staged.store(key_view, static_cast<double>(*number)));
            break;
        }
        case SpanAttributeType::Bool: {
            const auto* const flag = value.get_ptr<const nlohmann::json::boolean_t*>();
            if (flag == nullptr) { return; }
            static_cast<void>(staged.store(key_view, static_cast<bool>(*flag)));
            break;
        }
        case SpanAttributeType::String: {
            const auto* const text = value.get_ptr<const nlohmann::json::string_t*>();
            if (text == nullptr) { return; }
            const std::string_view text_view(*text);
            // Inline-representable strings restore exactly; LONGER strings
            // are producer-owned views that deserialization cannot rebind —
            // the attribute stays untouched (documented bound).
            if (text_view.size() > attribute_inline_text_capacity) {
                return;
            }
            static_cast<void>(staged.store(key_view, text_view));
            break;
        }
        default:
            return; // unknown tag: untouched
    }
    out = staged;
}

// --- SpanRecord -------------------------------------------------------------

std::string SpanRecord::to_string() const {
    std::string out = "SpanRecord(trace_id=" + trace_id.to_string();
    out += ", span_id=" + span_id.to_string();
    out += ", parent_span_id=" + parent_span_id.to_string();
    out += ", kind=" + std::string(tracing::to_string(kind));
    out += ", status=" + std::string(tracing::to_string(status));
    out += ", flags=" + std::to_string(flags);
    out += ", thread_id=" + std::to_string(thread_id);
    out += ", start_ns=" + std::to_string(start.nanoseconds());
    out += ", end_ns=" + std::to_string(end.nanoseconds());
    out += ", duration_ns=" + std::to_string(duration_ns());
    out += ", name=" + std::string(name_view());
    out += ", file=" + std::string(file_name != nullptr ? file_name : "");
    out += ":" + std::to_string(line);
    out += ", function=" + std::string(function_name != nullptr ? function_name : "");
    out += ", column=" + std::to_string(column);
    out += ", attributes=[";
    for (std::size_t i = 0; i < attribute_count && i < MaxAttributes; ++i) {
        if (i > 0U) {
            out += ", ";
        }
        out += attributes[i].to_string();
    }
    out += "])";
    return out;
}

std::string to_string(const SpanRecord& record) { return record.to_string(); }

void to_json(nlohmann::json& j, const SpanRecord& record) {
    nlohmann::json attributes = nlohmann::json::array();
    for (std::size_t i = 0; i < record.attribute_count && i < MaxAttributes; ++i) {
        attributes.push_back(record.attributes[i]);
    }
    j = nlohmann::json{
        {"trace_id", record.trace_id},
        {"span_id", record.span_id},
        {"parent_span_id", record.parent_span_id},
        {"kind", record.kind},
        {"status", record.status},
        {"flags", record.flags},
        {"attribute_count", record.attribute_count},
        {"thread_id", record.thread_id},
        {"start_ns", record.start.nanoseconds()},
        {"end_ns", record.end.nanoseconds()},
        {"name", std::string(record.name_view())},
        {"file", std::string(record.file_name != nullptr ? record.file_name : "")},
        {"function", std::string(record.function_name != nullptr ? record.function_name : "")},
        {"line", record.line},
        {"column", record.column},
        {"attributes", attributes},
    };
}

void from_json(const nlohmann::json& j, SpanRecord& out) noexcept {
    if (!j.is_object()) {
        return; // scalar/array/null: the record is unchanged
    }
    const auto trace = j.find("trace_id");
    if (trace != j.end()) { nlohmann::from_json(*trace, out.trace_id); }
    const auto span = j.find("span_id");
    if (span != j.end()) { nlohmann::from_json(*span, out.span_id); }
    const auto parent = j.find("parent_span_id");
    if (parent != j.end()) { nlohmann::from_json(*parent, out.parent_span_id); }
    const auto kind = j.find("kind");
    if (kind != j.end()) { nlohmann::from_json(*kind, out.kind); }
    const auto status = j.find("status");
    if (status != j.end()) { nlohmann::from_json(*status, out.status); }
    const auto flags = j.find("flags");
    if (flags != j.end() && flags->is_number_unsigned()) {
        out.flags = static_cast<std::uint8_t>(flags->get<std::uint64_t>());
    }
    const auto attribute_count = j.find("attribute_count");
    if (attribute_count != j.end() && attribute_count->is_number_unsigned()) {
        out.attribute_count =
            static_cast<std::uint8_t>(attribute_count->get<std::uint64_t>());
    }
    const auto thread_id = j.find("thread_id");
    if (thread_id != j.end() && thread_id->is_number_unsigned()) {
        out.thread_id = static_cast<std::uint32_t>(thread_id->get<std::uint64_t>());
    }
    const auto start_ns = j.find("start_ns");
    if (start_ns != j.end() && start_ns->is_number_unsigned()) {
        out.start = chrono::Timestamp{start_ns->get<std::uint64_t>()};
    }
    const auto end_ns = j.find("end_ns");
    if (end_ns != j.end() && end_ns->is_number_unsigned()) {
        out.end = chrono::Timestamp{end_ns->get<std::uint64_t>()};
    }
    const auto name = j.find("name");
    if (name != j.end() && name->is_string()) {
        const auto* const text = name->get_ptr<const nlohmann::json::string_t*>();
        if (text != nullptr) {
            const std::size_t take = text->size() <= SpanRecord::name_capacity
                                         ? text->size()
                                         : SpanRecord::name_capacity;
            for (std::size_t i = 0; i < take; ++i) {
                out.name[i] = (*text)[i];
            }
            out.name[take] = '\0';
        }
    }
    const auto file = j.find("file");
    if (file != j.end() && file->is_string()) {
        const auto* const text = file->get_ptr<const nlohmann::json::string_t*>();
        if (text != nullptr) {
            // Restored location text is interned into stable process-static
            // storage (see Location_intern): pointers stay valid, texts that
            // do not fit leave the field unchanged.
            const char* const interned = location_intern().intern(std::string_view(*text));
            if (interned != nullptr) {
                out.file_name = interned;
            }
        }
    }
    const auto function = j.find("function");
    if (function != j.end() && function->is_string()) {
        const auto* const text = function->get_ptr<const nlohmann::json::string_t*>();
        if (text != nullptr) {
            const char* const interned = location_intern().intern(std::string_view(*text));
            if (interned != nullptr) {
                out.function_name = interned;
            }
        }
    }
    const auto line = j.find("line");
    if (line != j.end() && line->is_number_unsigned()) {
        out.line = static_cast<std::uint32_t>(line->get<std::uint64_t>());
    }
    const auto column = j.find("column");
    if (column != j.end() && column->is_number_unsigned()) {
        out.column = static_cast<std::uint32_t>(column->get<std::uint64_t>());
    }
    const auto attributes = j.find("attributes");
    if (attributes != j.end() && attributes->is_array()) {
        std::uint8_t count = 0;
        for (const auto& element : *attributes) {
            if (count >= MaxAttributes) { break; }
            SpanAttribute staged{};
            nlohmann::from_json(element, staged);
            // A parsed attribute always carries a key and the correct tag; a
            // default-constructed staging slot means the element did not
            // parse cleanly — skipped without touching the rest.
            if (staged.key_view().empty() && staged.type == SpanAttributeType::Int64) {
                continue;
            }
            out.attributes[count] = staged;
            ++count;
        }
        out.attribute_count = count;
    }
}

// --- TracerProvider_config / Dropped_stats (declared in tracer_provider.hpp;

// bodies here so the hot headers stay free of cold serialization machinery).

std::string TracerProvider_config::to_string() const {
    return "TracerProvider_config(core_mask=0x" + std::to_string(core_mask) +
           ", max_tracers=" + std::to_string(max_tracers) +
           ", max_spans_per_window=" + std::to_string(max_spans_per_window) +
           ", window_ns=" + std::to_string(window_ns) + ")";
}

std::string to_string(const TracerProvider_config& config) { return config.to_string(); }

void to_json(nlohmann::json& j, const TracerProvider_config& config) {
    j = nlohmann::json{
        {"core_mask", config.core_mask},
        {"max_tracers", config.max_tracers},
        {"max_spans_per_window", config.max_spans_per_window},
        {"window_ns", config.window_ns},
    };
}

void from_json(const nlohmann::json& j, TracerProvider_config& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto core_mask = j.find("core_mask");
    if (core_mask != j.end() && core_mask->is_number_unsigned()) {
        out.core_mask = core_mask->get<std::uint64_t>();
    }
    const auto max_tracers = j.find("max_tracers");
    if (max_tracers != j.end() && max_tracers->is_number_unsigned()) {
        out.max_tracers = static_cast<std::uint32_t>(max_tracers->get<std::uint64_t>());
    }
    const auto max_spans = j.find("max_spans_per_window");
    if (max_spans != j.end() && max_spans->is_number_unsigned()) {
        out.max_spans_per_window = max_spans->get<std::uint64_t>();
    }
    const auto window_ns = j.find("window_ns");
    if (window_ns != j.end() && window_ns->is_number_unsigned()) {
        out.window_ns = window_ns->get<std::uint64_t>();
    }
}

std::string Dropped_stats::to_string() const {
    return "Dropped_stats(rate_limited=" + std::to_string(rate_limited) +
           ", buffer_full=" + std::to_string(buffer_full) +
           ", slot_exhausted=" + std::to_string(slot_exhausted) +
           ", total=" + std::to_string(total()) + ")";
}

std::string to_string(const Dropped_stats& stats) { return stats.to_string(); }

void to_json(nlohmann::json& j, const Dropped_stats& stats) {
    j = nlohmann::json{
        {"rate_limited", stats.rate_limited},
        {"buffer_full", stats.buffer_full},
        {"slot_exhausted", stats.slot_exhausted},
    };
}

void from_json(const nlohmann::json& j, Dropped_stats& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto rate_limited = j.find("rate_limited");
    if (rate_limited != j.end() && rate_limited->is_number_unsigned()) {
        out.rate_limited = rate_limited->get<std::uint64_t>();
    }
    const auto buffer_full = j.find("buffer_full");
    if (buffer_full != j.end() && buffer_full->is_number_unsigned()) {
        out.buffer_full = buffer_full->get<std::uint64_t>();
    }
    const auto slot_exhausted = j.find("slot_exhausted");
    if (slot_exhausted != j.end() && slot_exhausted->is_number_unsigned()) {
        out.slot_exhausted = slot_exhausted->get<std::uint64_t>();
    }
}

} // namespace nexigen::tracing
