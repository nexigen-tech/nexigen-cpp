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
#include <nexigen/core/sink_io.hpp>
#include <nexigen/tracing/chrome_trace_sink_config.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/detail/json_writer.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// @brief Chrome Trace Event sink: one Perfetto-loadable JSON object per run.
///
/// `start()` truncates the configured file and writes the `traceEvents` array
/// header; every `on_span` appends one complete (`"ph": "X"`) event, and
/// `stop()` writes the closing footer so the whole file parses as a single
/// JSON value. Timestamps are integer microseconds relative to the first
/// drained span, the process id is cached once at `start()`, and `args`
/// carries the trace/span/parent ids as hexadecimal strings plus every typed
/// attribute.
///
/// All state is consumer-thread plain state: the provider's single background
/// worker is the only caller. The event is composed in fixed scratch with no
/// allocation and no iostreams, so delivery never touches a producer thread.
///
/// @note Spec `tracing/trace-sinks`, requirement "ChromeTraceSink
///       Perfetto-compatible output".
class ChromeTraceSink {
public:
    /// @brief Construct the sink from its configuration; the file opens later.
    ///
    /// @param config Configuration carrying the trace path; copied by value so
    ///        the caller may release it immediately.
    /// @note Spec `tracing/trace-sinks`, requirement "ChromeTraceSink
    ///       Perfetto-compatible output".
    explicit ChromeTraceSink(const ChromeTraceSinkConfig& config) noexcept : config_(config) {}

    /// @brief Copy construction is deleted: the sink owns a descriptor.
    ChromeTraceSink(const ChromeTraceSink&) = delete;
    /// @brief Copy assignment is deleted: the sink owns a descriptor.
    ChromeTraceSink& operator=(const ChromeTraceSink&) = delete;

    /// @brief Move the descriptor, configuration, and timestamp base state.
    ///
    /// @param other Source sink; left closed (`fd_ == -1`) so neither
    ///        destructor can double-close.
    /// @warning A moved-from sink must not be used for delivery.
    ChromeTraceSink(ChromeTraceSink&& other) noexcept
        : config_(std::move(other.config_)), pid_(other.pid_), base_ns_(other.base_ns_),
          base_set_(other.base_set_), first_event_(other.first_event_), fd_(other.fd_) {
        other.fd_ = -1;
    }
    /// @brief Move assignment is deleted: rebinding a live sink is a
    ///        construction action, not an assignment.
    ChromeTraceSink& operator=(ChromeTraceSink&&) = delete;

    /// @brief Create/truncate the trace file and write the array header.
    ///
    /// Also caches the process id and resets the timestamp base and the
    /// first-event flag, so a restarted sink begins a fresh timeline.
    ///
    /// @return Success once the header is written; `SystemError` when the path
    ///         cannot be opened or the header write fails. An unopenable path
    ///         leaves no partial file content.
    /// @note Spec `tracing/trace-sinks`, requirement "ChromeTraceSink
    ///       Perfetto-compatible output".
    [[nodiscard]] core::Status start() noexcept {
        pid_ = static_cast<std::uint64_t>(::getpid());
        // Truncating open (append=false shares the policy with O_TRUNC).
        fd_ = core::sink_io::open_output(config_.path, /*append=*/false);
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

    /// @brief Write one complete Chrome event for one drained span.
    ///
    /// The first delivered span establishes the microsecond base, so its `ts`
    /// is `0`; later `ts` values are non-negative deltas and `dur` is the span
    /// duration truncated to microseconds. A record that pre-dates the base
    /// clamps to `0` rather than underflowing. The event is composed in fixed
    /// scratch and written whole, so a scratch overflow never emits a partial
    /// line.
    ///
    /// @param record Span to render; copied field-by-field into the event.
    /// @return Success when the full line was written; `BufferFull` when the
    ///         event does not fit the scratch (nothing is written), or
    ///         `SystemError` when no file is open or the write fails.
    /// @note Spec `tracing/trace-sinks`, requirement "ChromeTraceSink
    ///       Perfetto-compatible output"; the delivery path allocates nothing.
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
        const std::uint64_t ts_us = record.start.nanoseconds() >= base_ns_
                                        ? (record.start.nanoseconds() - base_ns_) / 1000U
                                        : 0ULL;
        const std::uint64_t dur_us = record.duration_ns() / 1000U;

        detail::FixedWriter writer{scratch_, detail::json_scratch_capacity};
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
                default: static_cast<void>(writer.append_sv("null")); break;
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

    /// @brief Synchronize the open trace file with `fsync`.
    ///
    /// @return Success when the descriptor is open and `fsync` succeeds;
    ///         `SystemError` when no file is open or synchronization fails.
    /// @note Spec `tracing/trace-sinks`, requirement "ChromeTraceSink
    ///       Perfetto-compatible output".
    [[nodiscard]] core::Status flush() noexcept {
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (::fsync(fd_) != 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// @brief Write the footer and close so the file parses as one object.
    ///
    /// Idempotent: an already-closed sink is a no-op.
    ///
    /// @note Spec `tracing/trace-sinks`, requirement "ChromeTraceSink
    ///       Perfetto-compatible output".
    void stop() noexcept {
        if (fd_ >= 0) {
            static_cast<void>(write_bytes("\n]}\n"));
            static_cast<void>(::close(fd_));
            fd_ = -1;
        }
    }

    /// @brief Render the sink state as a cold-path diagnostic string.
    ///
    /// @return Text naming the trace path and the live/open flags; allocates,
    ///         never called on the delivery path.
    /// @note Project mandate 9 (`to_string`).
    [[nodiscard]] std::string to_string() const;

    /// @brief Report whether the trace file is currently open.
    ///
    /// @return `true` while the sink holds a valid descriptor.
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

private:
    /// Write bytes with EINTR/partial-write retry; the length is derived
    /// from the literal (never including its NUL terminator).
    template <std::size_t Length>
    [[nodiscard]] bool write_bytes(const char (&bytes)[Length]) noexcept {
        return write_bytes(bytes, Length - 1U);
    }

    /// The pointer+length core of the writer. Body of the byte-composing
    /// loop is the shared sink_io implementation; the bool result maps its
    /// core::Status into this sink's existing bool contract.
    [[nodiscard]] bool write_bytes(const char* bytes, std::size_t length) noexcept {
        return core::sink_io::write_all(fd_, bytes, length).has_value();
    }

    ChromeTraceSinkConfig config_;
    std::uint64_t pid_{};     ///< cached at start()
    std::uint64_t base_ns_{}; ///< first drained span's start (mono ns)
    bool base_set_{false};
    bool first_event_{true};
    int fd_{-1};
    char scratch_[detail::json_scratch_capacity]{};
};

/// @brief Free diagnostic helper mirroring `ChromeTraceSink::to_string()`.
///
/// @param sink Sink to describe.
/// @return The same text the member returns.
/// @note Project mandate 9 (`to_string`); cold path only.
[[nodiscard]] std::string to_string(const ChromeTraceSink& sink);

/// @brief Serialize the live sink's configuration and state as JSON.
///
/// @param j Destination JSON value, overwritten with the diagnostic object.
/// @param sink Sink to describe.
/// @note Spec `tracing/trace-sinks`, requirement "Live sink serialization
///       surfaces"; project mandate 10 (JSON). No `from_json` exists: live
///       sinks are constructed from configuration, not parsed from data.
void to_json(nlohmann::json& j, const ChromeTraceSink& sink);

static_assert(concepts::TraceSink<ChromeTraceSink>,
              "ChromeTraceSink must satisfy the sink contract");

} // namespace nexigen::tracing

/// @brief `std::formatter` for `ChromeTraceSink`, rendering its diagnostic text.
///
/// @note Project mandate 11 (`std::formatter`); formatting is cold path only.
template <>
struct std::formatter<nexigen::tracing::ChromeTraceSink> : std::formatter<std::string_view> {
    /// @brief Format the sink through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param sink Sink to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::ChromeTraceSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
