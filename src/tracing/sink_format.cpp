// nexigen::tracing — sink/config serialization bodies (design D2/D11): the
// cold to_string/to_json/from_json surfaces of the file sinks and their
// configuration value types, plus the JSONL line/window rendering. All of
// this runs on the consumer thread or at configuration time — never on a
// producer hot path.
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/time_format.hpp>
#include <nexigen/tracing/chrome_trace_sink.hpp>
#include <nexigen/tracing/console_trace_sink.hpp>
#include <nexigen/tracing/detail/json_writer.hpp>
#include <nexigen/tracing/json_file_trace_sink.hpp>

namespace nexigen::tracing {
namespace {

/// Zero-padded decimal append into a char buffer (fixed-width fields).
void append_padded(char* out, std::size_t& len, std::int64_t value,
                   std::size_t width) noexcept {
    char digits[24];
    const auto result = std::to_chars(digits, digits + sizeof(digits), value);
    if (result.ec != std::errc{}) {
        return;
    }
    const std::size_t digits_len = static_cast<std::size_t>(result.ptr - digits);
    for (std::size_t i = digits_len; i < width; ++i) {
        out[len++] = '0';
    }
    for (std::size_t i = 0; i < digits_len && len < 64U; ++i) {
        out[len++] = digits[i];
    }
}

} // namespace

// --- ConsoleTraceSink -------------------------------------------------------

void to_json(nlohmann::json& j, const ConsoleTraceSink& sink) {
    j = nlohmann::json{{"type", "ConsoleTraceSink"}, {"fd", sink.descriptor()}};
}

// --- JsonFileTraceSink_config -----------------------------------------------

std::string JsonFileTraceSink_config::to_string() const {
    return "JsonFileTraceSink_config(path=" + path +
           ", rotation=" + std::string(tracing::to_string(rotation)) +
           ", max_spans_per_window=" + std::to_string(max_spans_per_window) +
           ", window_ns=" + std::to_string(window_ns) + ")";
}

std::string to_string(const JsonFileTraceSink_config& config) {
    return config.to_string();
}

void to_json(nlohmann::json& j, const JsonFileTraceSink_config& config) {
    j = nlohmann::json{
        {"path", config.path},
        {"rotation", config.rotation},
        {"max_spans_per_window", config.max_spans_per_window},
        {"window_ns", config.window_ns},
    };
}

void from_json(const nlohmann::json& j, JsonFileTraceSink_config& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto path = j.find("path");
    if (path != j.end() && path->is_string()) {
        out.path = path->get<std::string>();
    }
    const auto rotation = j.find("rotation");
    if (rotation != j.end()) {
        nlohmann::from_json(*rotation, out.rotation);
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

// --- JsonFileTraceSink ------------------------------------------------------

std::string_view JsonFileTraceSink::window_filename(std::int64_t utc_ns) noexcept {
    // Label: Hourly "YYYYMMDDTHH", Daily "YYYYMMDD" (fixed-width digits).
    const chrono::Civil_time civil = chrono::civil_from_utc_ns(utc_ns);
    char label[16];
    std::size_t label_len = 0;
    append_padded(label, label_len, civil.year, 4U);
    append_padded(label, label_len, civil.month, 2U);
    append_padded(label, label_len, civil.day, 2U);
    if (rotation_ == RotationPolicy::Hourly) {
        label[label_len++] = 'T';
        append_padded(label, label_len, civil.hour, 2U);
    }
    // "<prefix>.<label>" into the build buffer; empty view when it does
    // not fit (open_for_window maps that to InvalidArgument).
    if (path_len_ + 1U + label_len >= sizeof(file_)) {
        return std::string_view{};
    }
    std::size_t len = 0;
    for (std::size_t i = 0; i < path_len_; ++i) {
        file_[len++] = path_[i];
    }
    file_[len++] = '.';
    for (std::size_t i = 0; i < label_len; ++i) {
        file_[len++] = label[i];
    }
    file_[len] = '\0';
    return std::string_view(file_, len);
}

core::Status JsonFileTraceSink::write_record_line(const SpanRecord& record,
                                                  std::int64_t utc_ns) noexcept {
    const std::int64_t end_utc_ns =
        chrono::utc_from_mono_ns(record.end.nanoseconds(), offset_ns_);
    char start_stamp[31];
    char end_stamp[31];
    chrono::format_utc_iso8601_ns(utc_ns, start_stamp);
    chrono::format_utc_iso8601_ns(end_utc_ns, end_stamp);

    detail::Fixed_writer writer{scratch_, detail::json_scratch_capacity};
    static_cast<void>(writer.append_sv("{\"trace_id\":\""));
    static_cast<void>(writer.append_hex16(record.trace_id.value()));
    static_cast<void>(writer.append_sv("\",\"span_id\":\""));
    static_cast<void>(writer.append_hex16(record.span_id.value()));
    static_cast<void>(writer.append_sv("\",\"parent_span_id\":\""));
    static_cast<void>(writer.append_hex16(record.parent_span_id.value()));
    static_cast<void>(writer.append_sv("\",\"kind\":\""));
    static_cast<void>(writer.append_sv(tracing::to_string(record.kind)));
    static_cast<void>(writer.append_sv("\",\"status\":\""));
    static_cast<void>(writer.append_sv(tracing::to_string(record.status)));
    static_cast<void>(writer.append_sv("\",\"thread_id\":"));
    static_cast<void>(writer.append_u64(record.thread_id));
    static_cast<void>(writer.append_sv(",\"start\":\""));
    static_cast<void>(writer.append_sv(std::string_view(start_stamp, 30U)));
    static_cast<void>(writer.append_sv("\",\"end\":\""));
    static_cast<void>(writer.append_sv(std::string_view(end_stamp, 30U)));
    static_cast<void>(writer.append_sv("\",\"duration_ns\":"));
    static_cast<void>(writer.append_u64(record.duration_ns()));
    static_cast<void>(writer.append_sv(",\"name\":\""));
    static_cast<void>(writer.append_escaped(record.name_view()));
    static_cast<void>(writer.append_sv("\",\"file\":\""));
    static_cast<void>(writer.append_escaped(
        record.file_name != nullptr ? std::string_view(record.file_name)
                                    : std::string_view{}));
    static_cast<void>(writer.append_sv("\",\"function\":\""));
    static_cast<void>(writer.append_escaped(
        record.function_name != nullptr ? std::string_view(record.function_name)
                                        : std::string_view{}));
    static_cast<void>(writer.append_sv("\",\"line\":"));
    static_cast<void>(writer.append_u64(record.line));
    static_cast<void>(writer.append_sv(",\"column\":"));
    static_cast<void>(writer.append_u64(record.column));
    static_cast<void>(writer.append_sv(",\"attributes\":["));
    for (std::size_t i = 0; i < record.attribute_count && i < MaxAttributes; ++i) {
        const SpanAttribute& attr = record.attributes[i];
        if (i > 0U) {
            static_cast<void>(writer.append_char(','));
        }
        static_cast<void>(writer.append_sv("{\"key\":\""));
        static_cast<void>(writer.append_escaped(attr.key_view()));
        static_cast<void>(writer.append_sv("\",\"type\":\""));
        static_cast<void>(writer.append_sv(tracing::to_string(attr.type)));
        static_cast<void>(writer.append_sv("\",\"value\":"));
        switch (attr.type) {
            case SpanAttributeType::Int64:
                static_cast<void>(writer.append_i64(attr.value.i64));
                break;
            case SpanAttributeType::Uint64:
                static_cast<void>(writer.append_u64(attr.value.u64));
                break;
            case SpanAttributeType::Double:
                static_cast<void>(writer.append_double(attr.value.f64));
                break;
            case SpanAttributeType::Bool:
                static_cast<void>(writer.append_sv(attr.value.b ? "true" : "false"));
                break;
            case SpanAttributeType::String:
                static_cast<void>(writer.append_char('"'));
                static_cast<void>(writer.append_escaped(attr.text_view()));
                static_cast<void>(writer.append_char('"'));
                break;
            default:
                static_cast<void>(writer.append_sv("null"));
                break;
        }
        static_cast<void>(writer.append_char('}'));
    }
    static_cast<void>(writer.append_sv("]}\n"));
    if (!writer.ok()) {
        // The line does not fit the scratch: nothing partial is written
        // (spec "Open and write failures are visible" — no invalid JSON).
        return std::unexpected(core::ErrorCode::BufferFull);
    }
    return write_all(writer.size());
}

std::string JsonFileTraceSink::to_string() const {
    std::string out = "JsonFileTraceSink(rotation=";
    out += tracing::to_string(rotation_);
    out += ", open=" + std::string(is_open() ? "true" : "false");
    out += ", file=\"" + std::string(path_, path_len_) + "\"";
    out += ", max_spans_per_window=" + std::to_string(max_per_window_);
    out += ", window_ns=" + std::to_string(window_ns_);
    out += ", dropped_in_window=" + std::to_string(dropped_in_window_);
    out += ")";
    return out;
}

std::string to_string(const JsonFileTraceSink& sink) { return sink.to_string(); }

void to_json(nlohmann::json& j, const JsonFileTraceSink& sink) {
    j = nlohmann::json{
        {"type", "JsonFileTraceSink"},
        {"open", sink.is_open()},
        {"diagnostic", sink.to_string()},
    };
}

// --- ChromeTraceSink_config -------------------------------------------------

std::string ChromeTraceSink_config::to_string() const {
    return "ChromeTraceSink_config(path=" + path + ")";
}

std::string to_string(const ChromeTraceSink_config& config) {
    return config.to_string();
}

void to_json(nlohmann::json& j, const ChromeTraceSink_config& config) {
    j = nlohmann::json{{"path", config.path}};
}

void from_json(const nlohmann::json& j, ChromeTraceSink_config& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto path = j.find("path");
    if (path != j.end() && path->is_string()) {
        out.path = path->get<std::string>();
    }
}

// --- ChromeTraceSink --------------------------------------------------------

std::string ChromeTraceSink::to_string() const {
    return "ChromeTraceSink(path=" + config_.path +
           ", open=" + std::string(is_open() ? "true" : "false") + ")";
}

std::string to_string(const ChromeTraceSink& sink) { return sink.to_string(); }

void to_json(nlohmann::json& j, const ChromeTraceSink& sink) {
    j = nlohmann::json{{"type", "ChromeTraceSink"},
                       {"open", sink.is_open()},
                       {"diagnostic", sink.to_string()}};
}

} // namespace nexigen::tracing
