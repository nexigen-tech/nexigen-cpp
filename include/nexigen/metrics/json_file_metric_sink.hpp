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
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <unistd.h>

#include <cerrno>

#include <nexigen/core/result.hpp>
#include <nexigen/core/sink_io.hpp>
#include <nexigen/chrono/civil_time.hpp>
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/detail/json_writer.hpp>
#include <nexigen/metrics/json_file_metric_sink_config.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>

namespace nexigen::metrics {

/// A sink that appends one JSON object per batch to a file.
class JsonFileMetricSink {
public:
    /// Open the file. An unopenable path leaves the sink without a
    /// descriptor; on_batch then reports SystemError.
    explicit JsonFileMetricSink(const JsonFileMetricSinkConfig& config) noexcept
        : fd_(core::sink_io::open_output(config.path, config.append)) {
        const std::size_t take = config.app_name.size() < 63 ? config.app_name.size() : 63;
        for (std::size_t i = 0; i < take; ++i) {
            app_[i] = config.app_name[i];
        }
        app_len_ = take;
    }

    JsonFileMetricSink(const JsonFileMetricSink&) = delete;
    JsonFileMetricSink& operator=(const JsonFileMetricSink&) = delete;

    /// Move transfers the descriptor and leaves the source closed (fd = -1),
    /// so the destructors can never double-close (design D3).
    JsonFileMetricSink(JsonFileMetricSink&& other) noexcept
        : fd_(other.fd_), app_len_(other.app_len_) {
        std::memcpy(app_, other.app_, sizeof(app_));
        other.fd_ = -1;
    }

    JsonFileMetricSink& operator=(JsonFileMetricSink&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            app_len_ = other.app_len_;
            std::memcpy(app_, other.app_, sizeof(app_));
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
        detail::FixedWriter writer{scratch_, detail::json_scratch_capacity};
        // UTC ISO-8601 stamp of this collection window.
        char stamp[31];
        chrono::format_utc_iso8601_ns(batch.utc_ns, stamp);
        for (std::size_t i = 0; i < 31U; ++i) {
            stamp_text_[i] = stamp[i];
        }
        static_cast<void>(writer.append_sv("{\"timestamp\":\""));
        static_cast<void>(writer.append_sv(stamp_text_));
        static_cast<void>(writer.append_sv("\",\"app\":\""));
        static_cast<void>(writer.append_escaped(std::string_view(app_, app_len_)));
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
            static_cast<void>(
                writer.append_escaped(detail::view_name(record.identity.meter_version)));
            static_cast<void>(writer.append_sv("\",\"metric\":\""));
            static_cast<void>(
                writer.append_escaped(detail::view_name(record.identity.metric_name)));
            {
                const std::string_view unit = detail::view_name(record.identity.unit);
                if (!unit.empty()) {
                    static_cast<void>(writer.append_sv("\",\"unit\":\""));
                    static_cast<void>(writer.append_escaped(unit));
                }
            }
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
                case MetricKind::State:
                    static_cast<void>(writer.append_sv("\",\"value\":\""));
                    static_cast<void>(writer.append_escaped(detail::view_name(record.state.value)));
                    static_cast<void>(writer.append_sv("\""));
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
            // The batch does not fit the scratch: nothing partial is
            // written, and the loss is visible (spec metric-sinks
            // "Scratch overflow discards the batch and counts it" —
            // analysis #17).
            ++discarded_batches_;
            return std::unexpected(core::ErrorCode::BufferFull);
        }
        if (!core::sink_io::write_all(fd_, scratch_, writer.size()).has_value()) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// Push the data to the file.
    [[nodiscard]] core::Status flush() noexcept {
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (!core::sink_io::fsync_fd(fd_).has_value()) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "JsonFileMetricSink(fd=" + std::to_string(fd_) +
               ", discarded_batches=" + std::to_string(discarded_batches_) + ")";
    }

    /// True when the file was opened successfully.
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

private:
    int fd_{-1};
    std::size_t discarded_batches_{0}; ///< batches over the JSON scratch (P2-18)
    char app_[64]{};
    std::size_t app_len_{0};
    char stamp_text_[32]{};
    char scratch_[detail::json_scratch_capacity]{};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const JsonFileMetricSink& sink) {
    return sink.to_string();
}

/// Configuration JSON (cold path).
inline void to_json(nlohmann::json& j, const JsonFileMetricSinkConfig& config) {
    j = nlohmann::json{
        {"path", config.path}, {"app_name", config.app_name}, {"append", config.append}};
}

inline void from_json(const nlohmann::json& j, JsonFileMetricSinkConfig& out) noexcept {
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
    const auto app_it = j.find("app_name");
    if (app_it != j.end() && app_it->is_string()) {
        const auto* const text = app_it->get_ptr<const nlohmann::json::string_t*>();
        if (text != nullptr) {
            out.app_name = std::string(*text);
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

/// The file sink formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::JsonFileMetricSink> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::JsonFileMetricSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
