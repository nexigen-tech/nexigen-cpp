// nexigen::metrics — JSON-lines file sink (design D13; spec metrics/metric-sinks
// "JSON-lines file sink").
//
// Writes one JSON object per batch per line. The timestamp is the UTC
// ISO-8601 instant rendered through nexigen::chrono ("YYYY-MM-DDTHH:MM:SS
// .nnnnnnnnnZ"), the records are escaped and written with a raw ::write.
//
// The file is opened during construction (the sink owns the descriptor); the
// write path is allocation-free (fixed scratch + bounds-checked writer) and
// flush() pushes the data to the file with fsync.
#pragma once

#include <cstdint>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <unistd.h>

#include <cerrno>

#include <nexigen/core/result.hpp>
#include <nexigen/chrono/time_format.hpp>
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/detail/json_writer.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>

namespace nexigen::metrics {

/// File sink configuration: the path and whether batches are appended.
struct JsonFileMetricSink_config {
    std::string path{};
    bool append{true};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "JsonFileMetricSink_config(path=" + path + ", append=" + (append ? "true" : "false") + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const JsonFileMetricSink_config& config) {
    return config.to_string();
}

/// A sink that appends one JSON object per batch to a file.
class JsonFileMetricSink {
public:
    /// Open the file. An unopenable path leaves the sink without a
    /// descriptor; on_batch then reports SystemError.
    explicit JsonFileMetricSink(const JsonFileMetricSink_config& config) noexcept {
        const int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (config.append ? O_APPEND : O_TRUNC);
        fd_ = ::open(config.path.c_str(), flags, 0644);
    }

    JsonFileMetricSink(const JsonFileMetricSink&) = delete;
    JsonFileMetricSink& operator=(const JsonFileMetricSink&) = delete;

    /// Move transfers the descriptor and leaves the source closed (fd = -1),
    /// so the destructors can never double-close (design D3).
    JsonFileMetricSink(JsonFileMetricSink&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    JsonFileMetricSink& operator=(JsonFileMetricSink&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    ~JsonFileMetricSink() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    /// Write one batch as a single JSON line.
    [[nodiscard]] core::Status on_batch(const MetricBatchSnapshot& batch) noexcept {
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        detail::Fixed_writer writer{scratch_, detail::json_scratch_capacity};
        // UTC ISO-8601 stamp of this collection window.
        char stamp[31];
        chrono::format_utc_iso8601_ns(batch.utc_ns, stamp);
        for (std::size_t i = 0; i < 31U; ++i) {
            stamp_text_[i] = stamp[i];
        }
        static_cast<void>(writer.append_sv("{\"timestamp\":\""));
        static_cast<void>(writer.append_sv(stamp_text_));
        static_cast<void>(writer.append_sv("\",\"records\":["));
        for (std::uint32_t i = 0; i < batch.record_count; ++i) {
            if (batch.records == nullptr) {
                break;
            }
            if (i > 0U) {
                static_cast<void>(writer.append_char(','));
            }
            const MetricRecord& record = batch.records[i];
            static_cast<void>(writer.append_sv("{\"meter\":\""));
            static_cast<void>(writer.append_escaped(detail::view_name(record.identity.meter_name)));
            static_cast<void>(writer.append_sv("\",\"version\":\""));
            static_cast<void>(writer.append_escaped(detail::view_name(record.identity.meter_version)));
            static_cast<void>(writer.append_sv("\",\"metric\":\""));
            static_cast<void>(writer.append_escaped(detail::view_name(record.identity.metric_name)));
            static_cast<void>(writer.append_sv("\",\"kind\":\""));
            static_cast<void>(writer.append_escaped(metrics::to_string(record.kind)));
            static_cast<void>(writer.append_sv("\",\"access\":\""));
            static_cast<void>(writer.append_escaped(metrics::to_string(record.access)));
            static_cast<void>(writer.append_sv("\",\"reset\":\""));
            static_cast<void>(writer.append_escaped(metrics::to_string(record.reset)));
            switch (record.kind) {
                case MetricKind::Counter:
                    static_cast<void>(writer.append_sv("\",\"value\":"));
                    static_cast<void>(writer.append_u64(record.scalar.counter));
                    break;
                case MetricKind::Gauge:
                    static_cast<void>(writer.append_sv("\",\"value\":"));
                    if (record.scalar.is_double) {
                        static_cast<void>(writer.append_double(record.scalar.gauge_double));
                    } else {
                        static_cast<void>(writer.append_i64(record.scalar.gauge_int));
                    }
                    break;
                case MetricKind::DescriptiveStatistics:
                    static_cast<void>(writer.append_sv("\",\"count\":"));
                    static_cast<void>(writer.append_u64(record.statistics.count));
                    static_cast<void>(writer.append_sv(",\"min\":"));
                    static_cast<void>(writer.append_double(record.statistics.min));
                    static_cast<void>(writer.append_sv(",\"max\":"));
                    static_cast<void>(writer.append_double(record.statistics.max));
                    static_cast<void>(writer.append_sv(",\"mean\":"));
                    static_cast<void>(writer.append_double(record.statistics.mean));
                    static_cast<void>(writer.append_sv(",\"std\":"));
                    static_cast<void>(writer.append_double(record.statistics.stddev));
                    break;
                default:
                    static_cast<void>(writer.append_sv("\",\"count\":"));
                    static_cast<void>(writer.append_u64(record.histogram.count));
                    static_cast<void>(writer.append_sv(",\"sum\":"));
                    static_cast<void>(writer.append_double(record.histogram.sum));
                    static_cast<void>(writer.append_sv(",\"min\":"));
                    static_cast<void>(writer.append_double(record.histogram.min));
                    static_cast<void>(writer.append_sv(",\"max\":"));
                    static_cast<void>(writer.append_double(record.histogram.max));
                    static_cast<void>(writer.append_sv(",\"p50\":"));
                    static_cast<void>(writer.append_double(record.histogram.p50));
                    static_cast<void>(writer.append_sv(",\"p90\":"));
                    static_cast<void>(writer.append_double(record.histogram.p90));
                    static_cast<void>(writer.append_sv(",\"p99\":"));
                    static_cast<void>(writer.append_double(record.histogram.p99));
                    static_cast<void>(writer.append_sv(",\"p999\":"));
                    static_cast<void>(writer.append_double(record.histogram.p999));
                    break;
            }
            static_cast<void>(writer.append_char('}'));
        }
        static_cast<void>(writer.append_sv("]}\n"));
        if (!writer.ok()) {
            // The batch does not fit the scratch: nothing partial is written.
            return std::unexpected(core::ErrorCode::BufferFull);
        }
        if (!write_all(writer.size())) {
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

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "JsonFileMetricSink(fd=" + std::to_string(fd_) + ")";
    }

    /// True when the file was opened successfully.
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

private:
    [[nodiscard]] bool write_all(std::size_t bytes) noexcept {
        std::size_t offset = 0;
        while (offset < bytes) {
            const ssize_t written = ::write(fd_, scratch_ + offset, bytes - offset);
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

    int fd_{-1};
    char stamp_text_[32]{};
    char scratch_[detail::json_scratch_capacity]{};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const JsonFileMetricSink& sink) { return sink.to_string(); }

/// Configuration JSON (cold path).
inline void to_json(nlohmann::json& j, const JsonFileMetricSink_config& config) {
    j = nlohmann::json{{"path", config.path}, {"append", config.append}};
}

inline void from_json(const nlohmann::json& j, JsonFileMetricSink_config& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto it = j.find("path");
    if (it != j.end() && it->is_string()) {
        const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
        if (text != nullptr) {
            out.path = std::string(*text);
        }
    }
    const auto append_it = j.find("append");
    if (append_it != j.end() && append_it->is_boolean()) {
        out.append = append_it->get<bool>();
    }
}

/// The sink itself is not deserializable: rebinding it opens a file, which is
/// a construction-time action, not a deserialization.
inline void to_json(nlohmann::json& j, const JsonFileMetricSink& sink) {
    j = nlohmann::json{{"type", "JsonFileMetricSink"}, {"open", sink.is_open()}};
}

static_assert(concepts::MetricSink<JsonFileMetricSink>,
              "JsonFileMetricSink must satisfy the sink contract");

} // namespace nexigen::metrics

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::JsonFileMetricSink_config>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::JsonFileMetricSink_config& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

/// The file sink formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::JsonFileMetricSink> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::JsonFileMetricSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
