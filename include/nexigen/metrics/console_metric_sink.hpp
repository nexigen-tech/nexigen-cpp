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

/// A sink that writes human-readable metric lines to a file descriptor.
class ConsoleMetricSink {
public:
    explicit ConsoleMetricSink(int descriptor = STDOUT_FILENO) noexcept : fd_(descriptor) {}

    /// Render and write one batch.
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

    /// Console output is unbuffered: every write is already complete.
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "ConsoleMetricSink(fd=" + std::to_string(fd_) + ")";
    }

    /// The destination descriptor.
    [[nodiscard]] int descriptor() const noexcept { return fd_; }

private:
    static constexpr std::size_t line_capacity = 512;

    void append_char(char c) noexcept {
        if (len_ < line_capacity) {
            line_[len_++] = c;
        }
    }

    void append_str(std::string_view text) noexcept {
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
            append_str(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    void append_i64(std::int64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_str(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    void append_double(double value) noexcept {
        char digits[40];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_str(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
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
[[nodiscard]] inline std::string to_string(const ConsoleMetricSink& sink) { return sink.to_string(); }

/// Diagnostic JSON (cold path): the sink round-trips its descriptor.
inline void to_json(nlohmann::json& j, const ConsoleMetricSink& sink) {
    j = nlohmann::json{{"type", "ConsoleMetricSink"}, {"fd", sink.descriptor()}};
}

inline void from_json(const nlohmann::json& j, ConsoleMetricSink& sink) noexcept {
    const auto it = j.find("fd");
    if (it != j.end() && it->is_number_integer()) {
        const auto value = it->get<std::int64_t>();
        sink = ConsoleMetricSink{static_cast<int>(value)};
    }
}

static_assert(concepts::MetricSink<ConsoleMetricSink>,
              "ConsoleMetricSink must satisfy the sink contract");

} // namespace nexigen::metrics

/// The console sink formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::ConsoleMetricSink> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::ConsoleMetricSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
