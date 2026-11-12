// nexigen::tracing — cold-path rendering for SpanFrame (mandate 9–11; change
// add-iceoryx2-trace-sink; design D1). Every allocating/std::string/nlohmann
// body lives here so the wire header stays lean for producers.
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/tracing/span_frame.hpp>

namespace nexigen::tracing {
namespace {

/// Truncate a zero-copy long string attribute into the inline arm. The wire
/// frame must not carry a pointer, so anything over 23 bytes is clipped.
void force_inline_string(SpanAttribute& attribute) noexcept {
    if (attribute.type != SpanAttributeType::String || attribute.text_inline) {
        return;
    }
    const std::string_view text = attribute.text_view();
    const std::size_t take =
        text.size() < attribute_inline_text_capacity ? text.size() : attribute_inline_text_capacity;
    for (std::size_t i = 0; i < take; ++i) {
        attribute.value.inline_text.text[i] = text[i];
    }
    attribute.value.inline_text.length = static_cast<std::uint8_t>(take);
    attribute.text_inline = true;
}

[[nodiscard]] bool read_unsigned(const nlohmann::json& j, const char* key,
                                 std::uint64_t& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_unsigned()) {
        return false;
    }
    out = it->get<std::uint64_t>();
    return true;
}

void read_name(const nlohmann::json& j, char (&out)[SpanFrame::name_capacity + 1]) noexcept {
    const auto it = j.find("name");
    if (it == j.end() || !it->is_string()) {
        return;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return;
    }
    const std::size_t take =
        text->size() < SpanFrame::name_capacity ? text->size() : SpanFrame::name_capacity;
    for (std::size_t i = 0; i < take; ++i) {
        out[i] = (*text)[i];
    }
    out[take] = '\0';
}

void read_app_id(const nlohmann::json& j, char (&out)[4]) noexcept {
    const auto it = j.find("app_id");
    if (it == j.end() || !it->is_string()) {
        return;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return;
    }
    const std::size_t take = text->size() < sizeof(out) ? text->size() : sizeof(out);
    for (std::size_t i = 0; i < take; ++i) {
        out[i] = (*text)[i];
    }
    for (std::size_t i = take; i < sizeof(out); ++i) {
        out[i] = '\0'; // a shorter code must not inherit the previous tail
    }
}

} // namespace

std::string SpanFrame::to_string() const {
    std::string out = "SpanFrame(trace=" + trace_id.to_string() + ", span=" + span_id.to_string();
    out += ", parent=" + parent_span_id.to_string();
    out += ", kind=" + std::string{tracing::to_string(kind)};
    out += ", status=" + std::string{tracing::to_string(status)};
    out += ", name=" + std::string{name_view()};
    out += ", app=" + std::string{app_id_text()};
    out += ", pid=" + std::to_string(pid);
    out += ", seq=" + std::to_string(sequence);
    out += ", start_ns=" + std::to_string(start_ns);
    out += ", duration_ns=" + std::to_string(duration_ns());
    out += ", file_hash=" + std::format("{:#x}", file_hash);
    out += ", function_hash=" + std::format("{:#x}", function_hash);
    out += ", attributes=" + std::to_string(attribute_count);
    out += ", version=" + std::to_string(version) + ")";
    return out;
}

void to_json(nlohmann::json& j, const SpanFrame& frame) {
    nlohmann::json attributes = nlohmann::json::array();
    for (std::size_t i = 0; i < frame.attribute_count && i < MaxAttributes; ++i) {
        attributes.push_back(frame.attributes[i]);
    }
    j = nlohmann::json{
        {"trace_id", frame.trace_id},
        {"span_id", frame.span_id},
        {"parent_span_id", frame.parent_span_id},
        {"kind", frame.kind},
        {"status", frame.status},
        {"flags", frame.flags},
        {"attribute_count", frame.attribute_count},
        {"thread_id", frame.thread_id},
        {"start_ns", frame.start_ns},
        {"end_ns", frame.end_ns},
        {"file_hash", frame.file_hash},
        {"function_hash", frame.function_hash},
        {"line", frame.line},
        {"column", frame.column},
        {"name", std::string(frame.name_view())},
        {"attributes", attributes},
        {"app_id", std::string(frame.app_id_text())},
        {"pid", frame.pid},
        {"sequence", frame.sequence},
        {"publish_ts_ns", frame.publish_ts_ns},
        {"version", frame.version},
    };
}

void read_ids(const nlohmann::json& j, SpanFrame& out) noexcept {
    if (const auto it = j.find("trace_id"); it != j.end()) {
        nlohmann::from_json(*it, out.trace_id);
    }
    if (const auto it = j.find("span_id"); it != j.end()) {
        nlohmann::from_json(*it, out.span_id);
    }
    if (const auto it = j.find("parent_span_id"); it != j.end()) {
        nlohmann::from_json(*it, out.parent_span_id);
    }
}

void read_vocabulary(const nlohmann::json& j, SpanFrame& out) noexcept {
    if (const auto it = j.find("kind"); it != j.end()) {
        nlohmann::from_json(*it, out.kind);
    }
    if (const auto it = j.find("status"); it != j.end()) {
        nlohmann::from_json(*it, out.status);
    }
}

void read_span_numbers(const nlohmann::json& j, SpanFrame& out) noexcept {
    std::uint64_t number = 0U;
    if (read_unsigned(j, "flags", number)) {
        out.flags = static_cast<std::uint8_t>(number);
    }
    if (read_unsigned(j, "attribute_count", number)) {
        out.attribute_count = static_cast<std::uint8_t>(number);
    }
    if (read_unsigned(j, "thread_id", number)) {
        out.thread_id = static_cast<std::uint32_t>(number);
    }
    if (read_unsigned(j, "start_ns", number)) {
        out.start_ns = number;
    }
    if (read_unsigned(j, "end_ns", number)) {
        out.end_ns = number;
    }
}

void read_source_numbers(const nlohmann::json& j, SpanFrame& out) noexcept {
    std::uint64_t number = 0U;
    if (read_unsigned(j, "file_hash", number)) {
        out.file_hash = number;
    }
    if (read_unsigned(j, "function_hash", number)) {
        out.function_hash = number;
    }
    if (read_unsigned(j, "line", number)) {
        out.line = static_cast<std::uint32_t>(number);
    }
    if (read_unsigned(j, "column", number)) {
        out.column = static_cast<std::uint32_t>(number);
    }
}

void read_identity_numbers(const nlohmann::json& j, SpanFrame& out) noexcept {
    std::uint64_t number = 0U;
    if (read_unsigned(j, "pid", number)) {
        out.pid = static_cast<std::uint32_t>(number);
    }
    if (read_unsigned(j, "sequence", number)) {
        out.sequence = number;
    }
    if (read_unsigned(j, "publish_ts_ns", number)) {
        out.publish_ts_ns = number;
    }
    if (read_unsigned(j, "version", number)) {
        out.version = static_cast<std::uint16_t>(number);
    }
}

void read_attributes(const nlohmann::json& j, SpanFrame& out) noexcept {
    const auto attributes = j.find("attributes");
    std::uint8_t count = 0U;
    if (attributes != j.end() && attributes->is_array()) {
        for (const auto& element : *attributes) {
            if (count >= MaxAttributes) {
                break;
            }
            nlohmann::from_json(element, out.attributes[count]);
            force_inline_string(out.attributes[count]);
            ++count;
        }
    }
    out.attribute_count = count;
}

void from_json(const nlohmann::json& j, SpanFrame& out) noexcept {
    read_ids(j, out);
    read_vocabulary(j, out);
    read_span_numbers(j, out);
    read_source_numbers(j, out);
    read_identity_numbers(j, out);
    read_name(j, out.name);
    read_app_id(j, out.app_id);
    read_attributes(j, out);
}

} // namespace nexigen::tracing
