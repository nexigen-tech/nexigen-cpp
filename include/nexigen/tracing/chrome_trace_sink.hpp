// nexigen::tracing — Chrome Trace Event / Perfetto-compatible sink (design
// D11; spec tracing/trace-sinks "ChromeTraceSink Perfetto-compatible output").
//
// Produces ONE parseable JSON object for the whole trace:
//   {"traceEvents":[ {event}, {event}, ... ]}
// one complete event ("ph": "X") per span per line, with commas between
// events. Event fields: "name" (span name), "cat" (the span kind's
// canonical name — the record deliberately carries no tracer scope),
// "ts"/"dur" in INTEGER MICROSECONDS relative to the FIRST drained span's
// start (base, consumer-thread state; the first event's ts is 0), "pid"
// (cached getpid), "tid" (the record's cached thread id), and "args"
// carrying trace_id/span_id/parent_span_id as 16-char hex plus every typed
// span attribute. The file loads and renders a timeline at ui.perfetto.dev.
//
// The µs truncation is display-resolution only (design risk note: the JSONL
// sink retains full nanosecond stamps).
//
// COLD PATH ONLY (the provider's consumer thread); fixed scratch, no
// allocation, no iostreams.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error> // std::errc via to_chars usage

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/detail/json_writer.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// Chrome sink configuration (value type, full serialization trio; body in
/// src/tracing/sink_format.cpp).
struct ChromeTraceSink_config {
    std::string path{}; ///< the trace file to (re)create at start().

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const ChromeTraceSink_config& config);

/// ADL JSON (cold path, full round-trip); bodies in sink_format.cpp.
void to_json(nlohmann::json& j, const ChromeTraceSink_config& config);
void from_json(const nlohmann::json& j, ChromeTraceSink_config& out) noexcept;

/// The Chrome Trace Event sink.
class ChromeTraceSink {
public:
    explicit ChromeTraceSink(const ChromeTraceSink_config& config) noexcept
        : config_(config) {}

    ChromeTraceSink(const ChromeTraceSink&) = delete;
    ChromeTraceSink& operator=(const ChromeTraceSink&) = delete;

    /// Move transfers the descriptor, configuration, and base state; the
    /// source is left closed so the destructors never double-close.
    ChromeTraceSink(ChromeTraceSink&& other) noexcept
        : config_(std::move(other.config_)), pid_(other.pid_), base_ns_(other.base_ns_),
          base_set_(other.base_set_), first_event_(other.first_event_), fd_(other.fd_) {
        other.fd_ = -1;
    }
    ChromeTraceSink& operator=(ChromeTraceSink&&) = delete; // stateful sink

    /// Create/truncate the trace file and write the header. An unopenable
    /// path surfaces SystemError; nothing partial is written.
    [[nodiscard]] core::Status start() noexcept {
        pid_ = static_cast<std::uint64_t>(::getpid());
        fd_ = ::open(config_.path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        base_set_ = false;
        base_ns_ = 0;
        first_event_ = true;
        if (!write_bytes("{\"traceEvents\":[")) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// Write one complete event for one span.
    [[nodiscard]] core::Status on_span(const SpanRecord& record) noexcept {
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (!base_set_) {
            base_ns_ = record.start.nanoseconds(); // the first drained span
            base_set_ = true;
        }
        // (start - base) / 1000 in unsigned 64-bit; spans record after the
        // base, so the subtraction is well-ordered (a record that somehow
        // pre-dates the base clamps to 0 — non-negative by contract).
        const std::uint64_t ts_us =
            record.start.nanoseconds() >= base_ns_
                ? (record.start.nanoseconds() - base_ns_) / 1000U
                : 0ULL;
        const std::uint64_t dur_us = record.duration_ns() / 1000U;

        detail::Fixed_writer writer{scratch_, detail::json_scratch_capacity};
        if (!first_event_) {
            static_cast<void>(writer.append_char(','));
        }
        first_event_ = false;
        static_cast<void>(writer.append_sv("\n{\"name\":\""));
        static_cast<void>(writer.append_escaped(record.name_view()));
        static_cast<void>(writer.append_sv("\",\"cat\":\""));
        static_cast<void>(writer.append_sv(tracing::to_string(record.kind)));
        static_cast<void>(writer.append_sv("\",\"ph\":\"X\",\"ts\":"));
        static_cast<void>(writer.append_u64(ts_us));
        static_cast<void>(writer.append_sv(",\"dur\":"));
        static_cast<void>(writer.append_u64(dur_us));
        static_cast<void>(writer.append_sv(",\"pid\":"));
        static_cast<void>(writer.append_u64(pid_));
        static_cast<void>(writer.append_sv(",\"tid\":"));
        static_cast<void>(writer.append_u64(record.thread_id));
        static_cast<void>(writer.append_sv(",\"args\":{\"trace_id\":\""));
        static_cast<void>(writer.append_hex16(record.trace_id.value()));
        static_cast<void>(writer.append_sv("\",\"span_id\":\""));
        static_cast<void>(writer.append_hex16(record.span_id.value()));
        static_cast<void>(writer.append_sv("\",\"parent_span_id\":\""));
        static_cast<void>(writer.append_hex16(record.parent_span_id.value()));
        static_cast<void>(writer.append_char('"'));
        for (std::size_t i = 0; i < record.attribute_count && i < MaxAttributes; ++i) {
            const SpanAttribute& attr = record.attributes[i];
            static_cast<void>(writer.append_sv(",\""));
            static_cast<void>(writer.append_escaped(attr.key_view()));
            static_cast<void>(writer.append_sv("\":"));
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
        }
        static_cast<void>(writer.append_sv("}}"));
        if (!writer.ok()) {
            return std::unexpected(core::ErrorCode::BufferFull); // no partial line
        }
        if (!write_bytes(static_cast<const char*>(scratch_), writer.size())) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// Push the data to the file.
    [[nodiscard]] core::Status flush() noexcept {
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (::fsync(fd_) != 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// Write the footer and close: the file parses as one JSON object.
    void stop() noexcept {
        if (fd_ >= 0) {
            static_cast<void>(write_bytes("\n]}\n"));
            static_cast<void>(::close(fd_));
            fd_ = -1;
        }
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;

    /// True while the trace file is open.
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

private:
    /// Write bytes with EINTR/partial-write retry; the length is derived
    /// from the literal (never including its NUL terminator).
    template <std::size_t Length>
    [[nodiscard]] bool write_bytes(const char (&bytes)[Length]) noexcept {
        return write_bytes(bytes, Length - 1U);
    }

    /// The pointer+length core of the writer.
    [[nodiscard]] bool write_bytes(const char* bytes, std::size_t length) noexcept {
        std::size_t offset = 0;
        while (offset < length) {
            const ssize_t written = ::write(fd_, bytes + offset, length - offset);
            if (written > 0) {
                offset += static_cast<std::size_t>(written);
            } else if (written < 0 && errno == EINTR) {
                continue;
            } else {
                return false;
            }
        }
        return true;
    }

    ChromeTraceSink_config config_;
    std::uint64_t pid_{};      ///< cached at start()
    std::uint64_t base_ns_{};  ///< first drained span's start (mono ns)
    bool base_set_{false};
    bool first_event_{true};
    int fd_{-1};
    char scratch_[detail::json_scratch_capacity]{};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const ChromeTraceSink& sink);

/// Diagnostic JSON of the live state (cold path); deliberately NO from_json
/// (constructed from configuration, not parsed from data); body in
/// src/tracing/sink_format.cpp.
void to_json(nlohmann::json& j, const ChromeTraceSink& sink);

static_assert(concepts::TraceSink<ChromeTraceSink>,
              "ChromeTraceSink must satisfy the sink contract");

} // namespace nexigen::tracing

/// The configuration and the live sink format as their diagnostic texts
/// (mandate 11).
template <>
struct std::formatter<nexigen::tracing::ChromeTraceSink_config>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::ChromeTraceSink_config& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
template <>
struct std::formatter<nexigen::tracing::ChromeTraceSink> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::ChromeTraceSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
