// nexigen::tracing — console trace sink (design D11; spec tracing/trace-sinks
// "ConsoleTraceSink").
//
// Renders every drained span as ONE human-readable line to the configured
// descriptor (stdout by default) through fixed stack buffers and
// std::to_chars (allocation-free numerics; deliberately NOT iostreams —
// their lazy init, buffering, and locale state allocate and can block).
// Writes go through ::write with EINTR/partial-write retry; unrecoverable
// failures surface as SystemError from on_span WITHOUT aborting, and the
// sink stays usable for error reporting. flush() is a no-op success: the
// descriptor is unbuffered by contract.
//
// Line shape (spec "Console lines carry span identity and detail"):
//   [trace 0123456789abcdef] span=... parent=... (root|child) kind=Server
//   status=Ok name="query" dur=1234ns tid=42 at=engine.cpp:77
//   attrs={retries=3, ratio=0.5, ok=true, tag="hil"}
#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unistd.h>

#include <cerrno>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// A sink that writes one human-readable line per span to a descriptor.
class ConsoleTraceSink {
public:
    /// fd = 1 (stdout) by default; tests capture through a pipe fixture.
    explicit ConsoleTraceSink(int descriptor = 1) noexcept : fd_(descriptor) {}

    ConsoleTraceSink(const ConsoleTraceSink&) = delete;
    ConsoleTraceSink& operator=(const ConsoleTraceSink&) = delete;

    /// Move transfers the descriptor and leaves the source unusable (fd =
    /// -1) so the destructors can never double-close.
    ConsoleTraceSink(ConsoleTraceSink&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    ConsoleTraceSink& operator=(ConsoleTraceSink&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0 && fd_ != other.fd_) {
                static_cast<void>(::close(fd_));
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    /// Render and write one span line (no iostreams, no allocation).
    [[nodiscard]] core::Status on_span(const SpanRecord& record) noexcept {
        len_ = 0;
        append_sv("[trace ");
        append_hex16(record.trace_id.value());
        append_sv("] span=");
        append_hex16(record.span_id.value());
        append_sv(" parent=");
        append_hex16(record.parent_span_id.value());
        append_sv(record.parent_span_id.is_unset() ? " (root)" : " (child)");
        append_sv(" kind=");
        append_sv(tracing::to_string(record.kind));
        append_sv(" status=");
        append_sv(tracing::to_string(record.status));
        append_sv(" name=\"");
        append_sv(record.name_view()); // name is bounded inline text
        append_sv("\" dur=");
        append_u64(record.duration_ns());
        append_sv("ns tid=");
        append_u64(record.thread_id);
        append_sv(" at=");
        append_sv(record.file_name != nullptr ? std::string_view(record.file_name)
                                              : std::string_view{});
        append_char(':');
        append_u64(record.line);
        if (record.attribute_count > 0U) {
            append_sv(" attrs={");
            for (std::size_t i = 0; i < record.attribute_count && i < MaxAttributes; ++i) {
                if (i > 0U) {
                    append_sv(", ");
                }
                const SpanAttribute& attr = record.attributes[i];
                append_sv(attr.key_view());
                append_char('=');
                switch (attr.type) {
                    case SpanAttributeType::Int64:
                        append_i64(attr.value.i64);
                        break;
                    case SpanAttributeType::Uint64:
                        append_u64(attr.value.u64);
                        break;
                    case SpanAttributeType::Double:
                        append_double(attr.value.f64);
                        break;
                    case SpanAttributeType::Bool:
                        append_sv(attr.value.b ? "true" : "false");
                        break;
                    case SpanAttributeType::String:
                        append_char('"');
                        append_sv(attr.text_view()); // bounded render (view may be long)
                        append_char('"');
                        break;
                    default:
                        append_sv("Unknown");
                        break;
                }
            }
            append_char('}');
        }
        append_char('\n');
        if (!write_all()) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// Nothing to start for a raw descriptor.
    [[nodiscard]] core::Status start() noexcept { return {}; }

    /// Console output is unbuffered: every write is already complete.
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// Nothing to stop.
    void stop() noexcept {}

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "ConsoleTraceSink(fd=" + std::to_string(fd_) + ")";
    }

    /// The destination descriptor.
    [[nodiscard]] int descriptor() const noexcept { return fd_; }

private:
    static constexpr std::size_t line_capacity = 1024;

    void append_char(char c) noexcept {
        if (len_ < line_capacity) {
            line_[len_++] = c;
        }
    }

    void append_sv(std::string_view text) noexcept {
        const std::size_t room = (len_ < line_capacity) ? (line_capacity - len_) : 0U;
        const std::size_t take = text.size() < room ? text.size() : room;
        for (std::size_t i = 0; i < take; ++i) {
            line_[len_++] = text[i];
        }
    }

    void append_u64(std::uint64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_sv(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    void append_i64(std::int64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_sv(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    void append_double(double value) noexcept {
        char digits[40];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_sv(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    void append_hex16(std::uint64_t value) noexcept {
        char hex[16];
        for (std::size_t i = 0; i < 16U; ++i) {
            const unsigned shift = 60U - static_cast<unsigned>(i) * 4U;
            constexpr char digits[] = "0123456789abcdef";
            hex[i] = digits[(value >> shift) & 0xFULL];
        }
        append_sv(std::string_view(hex, 16U));
    }

    [[nodiscard]] bool write_all() noexcept {
        std::size_t offset = 0;
        while (offset < len_) {
            const ssize_t written = ::write(fd_, line_ + offset, len_ - offset);
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

    int fd_;
    char line_[line_capacity]{};
    std::size_t len_{0};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const ConsoleTraceSink& sink) {
    return sink.to_string();
}

/// Diagnostic JSON (cold path): the sink round-trips its descriptor. The
/// live sink has deliberately NO from_json — sinks are constructed from
/// configuration objects, not parsed from data (spec "Live sinks render
/// diagnostics and refuse deserialization"); body in sink_format.cpp.
void to_json(nlohmann::json& j, const ConsoleTraceSink& sink);

static_assert(concepts::TraceSink<ConsoleTraceSink>,
              "ConsoleTraceSink must satisfy the sink contract");

} // namespace nexigen::tracing

/// The console sink formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::tracing::ConsoleTraceSink> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::ConsoleTraceSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
