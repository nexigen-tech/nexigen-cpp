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

/// @brief Sink that writes one human-readable line per span to a descriptor.
///
/// Each `on_span` renders the full identity (trace/span/parent ids, kind,
/// status, name, duration, thread id, source location) and every attribute
/// into a fixed 1024-byte line buffer using `std::to_chars`, then writes it
/// with `::write` retrying `EINTR` and partial writes. No iostreams, no
/// per-record heap allocation, and no locale state are involved.
///
/// The descriptor is owned by the caller (stdout by default); the sink only
/// writes to it. Unrecoverable write failures surface as `SystemError`
/// without aborting, leaving the sink usable for error reporting. All state is
/// consumer-thread plain state.
///
/// @note Spec `tracing/trace-sinks`, requirement "ConsoleTraceSink".
class ConsoleTraceSink {
public:
    /// @brief Construct the sink around an already-open descriptor.
    ///
    /// @param descriptor Destination descriptor; defaults to `1` (stdout),
    ///        and tests capture output through a pipe fixture.
    /// @note The descriptor is borrowed, not owned: `stop()` never closes it.
    explicit ConsoleTraceSink(int descriptor = 1) noexcept : fd_(descriptor) {}

    /// @brief Copy construction is deleted: the sink renders into its own
    ///        scratch buffer.
    ConsoleTraceSink(const ConsoleTraceSink&) = delete;
    /// @brief Copy assignment is deleted: the sink renders into its own
    ///        scratch buffer.
    ConsoleTraceSink& operator=(const ConsoleTraceSink&) = delete;

    /// @brief Move the descriptor, leaving the source unusable.
    ///
    /// @param other Source sink; set to `fd_ == -1` so neither destructor can
    ///        double-close.
    /// @warning A moved-from sink must not be used for delivery.
    ConsoleTraceSink(ConsoleTraceSink&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

    /// @brief Move-assign the descriptor, closing the previous one.
    ///
    /// @param other Source sink; set to `fd_ == -1` afterwards.
    /// @return This sink.
    /// @warning Self-assignment is a no-op; the previous descriptor is closed
    ///          when it differs from the source.
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

    /// @brief Render and write one span line without allocation or iostreams.
    ///
    /// The line carries ids, root/child marker, kind, status, name, duration,
    /// thread id, source location, and every typed attribute; it is truncated
    /// to the fixed buffer rather than overflowing.
    ///
    /// @param record Span to render.
    /// @return Success when the whole line was written; `SystemError` when the
    ///         descriptor write fails, without aborting the process.
    /// @note Spec `tracing/trace-sinks`, requirement "ConsoleTraceSink";
    ///       delivery allocates nothing per record.
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
                    case SpanAttributeType::Int64:  append_i64(attr.value.i64); break;
                    case SpanAttributeType::Uint64: append_u64(attr.value.u64); break;
                    case SpanAttributeType::Double: append_double(attr.value.f64); break;
                    case SpanAttributeType::Bool:   append_sv(attr.value.b ? "true" : "false"); break;
                    case SpanAttributeType::String:
                        append_char('"');
                        append_sv(attr.text_view()); // bounded render (view may be long)
                        append_char('"');
                        break;
                    default: append_sv("Unknown"); break;
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

    /// @brief Complete a start request without doing any work.
    ///
    /// @return Success status, always; a raw descriptor needs no setup.
    /// @note Spec `tracing/trace-sinks`, requirement "ConsoleTraceSink".
    [[nodiscard]] core::Status start() noexcept { return {}; }

    /// @brief Complete a flush request without doing any work.
    ///
    /// @return Success status, always; the descriptor is unbuffered by
    ///         contract, so every write is already complete.
    /// @note Spec `tracing/trace-sinks`, requirement "ConsoleTraceSink".
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// @brief Complete a stop request without doing any work.
    ///
    /// The borrowed descriptor is deliberately left open.
    /// @note Spec `tracing/trace-sinks`, requirement "ConsoleTraceSink".
    void stop() noexcept {}

    /// @brief Render the sink state as a cold-path diagnostic string.
    ///
    /// @return Text naming the destination descriptor; allocates, cold path
    ///         only.
    /// @note Project mandate 9 (`to_string`).
    [[nodiscard]] std::string to_string() const {
        return "ConsoleTraceSink(fd=" + std::to_string(fd_) + ")";
    }

    /// @brief Report the destination descriptor.
    ///
    /// @return The descriptor passed at construction (`-1` after a move).
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

/// @brief Free diagnostic helper mirroring `ConsoleTraceSink::to_string()`.
///
/// @param sink Sink to describe.
/// @return The same text the member returns.
/// @note Project mandate 9 (`to_string`); cold path only.
[[nodiscard]] inline std::string to_string(const ConsoleTraceSink& sink) {
    return sink.to_string();
}

/// @brief Serialize the live sink's descriptor as JSON (cold path).
///
/// The live sink deliberately has no `from_json`: sinks are constructed from
/// configuration objects, not parsed from data (spec "Live sink serialization
/// surfaces").
///
/// @param j Destination JSON value, overwritten with the sink state.
/// @param sink Sink to describe.
/// @note Spec `tracing/trace-sinks`, requirement "Live sink serialization
///       surfaces"; project mandate 10 (JSON).
void to_json(nlohmann::json& j, const ConsoleTraceSink& sink);

static_assert(concepts::TraceSink<ConsoleTraceSink>,
              "ConsoleTraceSink must satisfy the sink contract");

} // namespace nexigen::tracing

/// @brief `std::formatter` for `ConsoleTraceSink`.
///
/// @note Project mandate 11 (`std::formatter`); formatting is cold path only.
template <>
struct std::formatter<nexigen::tracing::ConsoleTraceSink> : std::formatter<std::string_view> {
    /// @brief Format the sink through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param sink Sink to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::ConsoleTraceSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
