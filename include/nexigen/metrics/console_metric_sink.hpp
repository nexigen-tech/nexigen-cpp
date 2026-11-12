// nexigen::metrics — console sink (design D13; spec metrics/metric-sinks
// "Console metric sink").
//
// Renders every harvested record as one human-readable line and emits it
// with a raw ::write to the configured descriptor (stdout by default).
// Deliberately NOT iostreams: their lazy initialization, buffering and locale
// state allocate and can block.
//
// The rendering uses fixed stack buffers and std::to_chars (allocation-free
// numerics), so a batch never touches the heap. Partial writes and EINTR are
// retried; an unrecoverable write error is reported as SystemError.
#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unistd.h>

#include <cerrno>

#include <nexigen/core/result.hpp>
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>

namespace nexigen::metrics {

/// @brief Sink that writes one human-readable line per record to a descriptor.
///
/// Rendering uses fixed stack buffers and `std::to_chars`, so `on_batch` never
/// touches the heap; bytes go out through raw `::write` (no iostreams, whose
/// lazy initialization and locale state allocate and can block). Partial writes
/// and `EINTR` are retried. The sink holds no buffers between batches, so a
/// single instance may be driven by one harvester thread at a time.
///
/// @note Spec `metrics/metric-sinks`, requirement "Console metric sink".
class ConsoleMetricSink {
public:
    /// @brief Bind the sink to a destination descriptor.
    ///
    /// @param descriptor Open file descriptor that receives the rendered lines;
    ///        defaults to `STDOUT_FILENO`. The sink borrows it and never closes
    ///        it, so the caller keeps ownership for the sink's lifetime.
    explicit ConsoleMetricSink(int descriptor = STDOUT_FILENO) noexcept : fd_(descriptor) {}

    /// @brief Render every record of one batch and write it out.
    ///
    /// Each record becomes one line: `meter/version metric kind <payload>`,
    /// where the payload is `value=`, the statistics summary, or the histogram
    /// percentiles depending on `record.kind`.
    ///
    /// @param batch Snapshot harvested by the provider; `records` may be null,
    ///        in which case nothing is written.
    /// @return Success, or `core::ErrorCode::SystemError` when a write fails
    ///         unrecoverably after the `EINTR`/partial-write retries.
    /// @note Spec `metrics/metric-sinks`, requirement "Console metric sink".
    [[nodiscard]] core::Status on_batch(const MetricBatchSnapshot& batch) noexcept {
        for (std::uint32_t i = 0; i < batch.record_count; ++i) {
            if (batch.records == nullptr) {
                break;
            }
            const MetricRecord& record = batch.records[i];
            len_ = 0;
            append_str(detail::view_name(record.identity.meter_name));
            append_char('/');
            append_str(detail::view_name(record.identity.meter_version));
            append_char(' ');
            append_str(detail::view_name(record.identity.metric_name));
            append_char(' ');
            append_str(metrics::to_string(record.kind));
            switch (record.kind) {
                case MetricKind::Counter:
                    append_str(" value=");
                    append_u64(record.scalar.counter);
                    break;
                case MetricKind::Gauge:
                    append_str(" value=");
                    if (record.scalar.is_double) {
                        append_double(record.scalar.gauge_double);
                    } else {
                        append_i64(record.scalar.gauge_int);
                    }
                    break;
                case MetricKind::DescriptiveStatistics:
                    append_str(" count=");
                    append_u64(record.statistics.count);
                    append_str(" min=");
                    append_double(record.statistics.min);
                    append_str(" max=");
                    append_double(record.statistics.max);
                    append_str(" mean=");
                    append_double(record.statistics.mean);
                    append_str(" std=");
                    append_double(record.statistics.stddev);
                    break;
                default:
                    append_str(" count=");
                    append_u64(record.histogram.count);
                    append_str(" sum=");
                    append_double(record.histogram.sum);
                    append_str(" min=");
                    append_double(record.histogram.min);
                    append_str(" max=");
                    append_double(record.histogram.max);
                    append_str(" p50=");
                    append_double(record.histogram.p50);
                    append_str(" p90=");
                    append_double(record.histogram.p90);
                    append_str(" p99=");
                    append_double(record.histogram.p99);
                    append_str(" p999=");
                    append_double(record.histogram.p999);
                    break;
            }
            append_char('\n');
            if (!write_all()) {
                return std::unexpected(core::ErrorCode::SystemError);
            }
        }
        return {};
    }

    /// @brief Complete a flush request; console output is already unbuffered.
    ///
    /// @return Success status, always: every line is written before `on_batch`
    ///         returns.
    /// @note Spec `metrics/metric-sinks`, requirement "Console metric sink".
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// @brief Render the diagnostic string, including the destination descriptor.
    ///
    /// @return `"ConsoleMetricSink(fd=<n>)"`; allocates, cold path only.
    /// @note Spec `metrics/metric-sinks`, scenario "Console sink diagnostics
    ///       contract"; project mandate 9 (`to_string`).
    [[nodiscard]] std::string to_string() const {
        return "ConsoleMetricSink(fd=" + std::to_string(fd_) + ")";
    }

    /// @brief Read the destination descriptor this sink writes to.
    ///
    /// @return The descriptor supplied at construction.
    [[nodiscard]] int descriptor() const noexcept { return fd_; }

private:
    /// Maximum rendered line length in bytes; longer lines are truncated.
    static constexpr std::size_t line_capacity = 512;

    /// @brief Append one byte to the line buffer, ignoring overflow.
    ///
    /// @param c Byte to append.
    /// @warning Silent truncation at `line_capacity`; the write path never
    ///          overflows, it only shortens the line.
    void append_char(char c) noexcept {
        if (len_ < line_capacity) {
            line_[len_++] = c;
        }
    }

    /// @brief Append a string view to the line buffer, truncating on overflow.
    ///
    /// @param text Bytes to append; only the part that fits is copied.
    void append_str(std::string_view text) noexcept {
        const std::size_t room = (len_ < line_capacity) ? (line_capacity - len_) : 0U;
        const std::size_t take = text.size() < room ? text.size() : room;
        for (std::size_t i = 0; i < take; ++i) {
            line_[len_++] = text[i];
        }
    }

    /// @brief Append an unsigned integer through the allocation-free `to_chars`.
    ///
    /// @param value Value to render.
    void append_u64(std::uint64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_str(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    /// @brief Append a signed integer through the allocation-free `to_chars`.
    ///
    /// @param value Value to render.
    void append_i64(std::int64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_str(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    /// @brief Append a double through the allocation-free `to_chars`.
    ///
    /// @param value Value to render.
    void append_double(double value) noexcept {
        char digits[40];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_str(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    /// @brief Write the whole line buffer to the descriptor, retrying short writes.
    ///
    /// @return `true` when every buffered byte was written; `false` on any error
    ///         other than `EINTR`.
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

/// @brief Free `to_string` helper mirroring the member function.
///
/// @param sink Sink to describe.
/// @return The same text as `ConsoleMetricSink::to_string`.
/// @note Spec `metrics/metric-sinks`, scenario "Console sink diagnostics
///       contract"; project mandate 9.
[[nodiscard]] inline std::string to_string(const ConsoleMetricSink& sink) {
    return sink.to_string();
}

/// @brief Serialize the console sink, including its destination descriptor.
///
/// Declarations only: bodies live in `console_metric_sink.cpp`.
///
/// @param j Destination JSON value.
/// @param sink Sink to describe.
/// @note Spec `metrics/metric-sinks`, scenario "Console sink diagnostics
///       contract"; project mandate 10 (JSON).
void to_json(nlohmann::json& j, const ConsoleMetricSink& sink);

/// @brief Restore the console sink descriptor from JSON; never throws.
///
/// Declarations only: bodies live in `console_metric_sink.cpp`.
///
/// @param j Source JSON value.
/// @param sink Sink whose descriptor is rebound; unchanged on malformed input.
/// @note Spec `metrics/metric-sinks`, scenario "Console sink diagnostics
///       contract"; must stay non-throwing under `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, ConsoleMetricSink& sink) noexcept;

static_assert(concepts::MetricSink<ConsoleMetricSink>,
              "ConsoleMetricSink must satisfy the sink contract");

} // namespace nexigen::metrics

/// @brief `std::formatter` for the console sink; renders its diagnostic string.
///
/// @note Spec `metrics/metric-sinks`, scenario "Console sink diagnostics
///       contract"; project mandate 11 (`std::formatter`).
template <>
struct std::formatter<nexigen::metrics::ConsoleMetricSink> : std::formatter<std::string_view> {
    /// @brief Write the sink's `to_string` text through the string formatter.
    ///
    /// @tparam Ctx Formatting context type supplied by `std::format`.
    /// @param sink Sink being formatted.
    /// @param ctx Destination formatting context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::metrics::ConsoleMetricSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
