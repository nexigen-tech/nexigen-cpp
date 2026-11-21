// nexigen-recd — telemetry session recorder (change record-telemetry-sessions).
//
// Subscribes to the suite log, metric, and trace services plus any configured
// event services and writes their frames into one recorder session.
#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <nexigen/chrono/boot_identity.hpp>
#include <nexigen/chrono/utc_offset.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/raw_subscriber.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/transport_header.hpp>
#include <nexigen/logging/log_frame.hpp>
#include <nexigen/metrics/metric_frame.hpp>
#include <nexigen/recorder/builtin_schemas.hpp>
#include <nexigen/recorder/session_writer.hpp>
#include <nexigen/tracing/span_frame.hpp>

#include "event_gap_tracker.hpp"
#include "recorder_daemon_config.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) {
    g_stop = 1;
}

using namespace nexigen;

using Options = recorder::RecorderDaemonConfig;

using LogSub = com::Subscriber<logging::LogFrame, com::TransportHeader>;
using MetricSub = com::Subscriber<metrics::MetricFrame, com::TransportHeader>;
using TraceSub = com::Subscriber<tracing::SpanFrame, com::TransportHeader>;

[[nodiscard]] bool take_value(int argc, char** argv, int& index, std::string& out) {
    if (index + 1 >= argc) {
        return false;
    }
    out = argv[++index];
    return true;
}

template <std::size_t N>
std::string*
find_value_option(const std::array<std::pair<std::string_view, std::string*>, N>& options,
                  std::string_view arg) {
    for (const auto& [name, target] : options) {
        if (arg == name) {
            return target;
        }
    }
    return nullptr;
}

struct FlagOption {
    std::string_view name;
    bool* target;
    bool value;
};

template <std::size_t N>
bool apply_flag(const std::array<FlagOption, N>& options, std::string_view arg) {
    for (const auto& option : options) {
        if (arg == option.name) {
            *option.target = option.value;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool parse_options(int argc, char** argv, Options& out) {
    const std::array<std::pair<std::string_view, std::string*>, 4> value_options{{
        {"--session-root", &out.session_root},
        {"--service-log", &out.service_log},
        {"--service-metrics", &out.service_metrics},
        {"--service-traces", &out.service_traces},
    }};
    const std::array<FlagOption, 7> flag_options{{
        {"--logs", &out.logs, true},
        {"--metrics", &out.metrics, true},
        {"--traces", &out.traces, true},
        {"--events", &out.events, true},
        {"--no-logs", &out.logs, false},
        {"--no-metrics", &out.metrics, false},
        {"--no-traces", &out.traces, false},
    }};

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--event-service") {
            std::string value;
            if (!take_value(argc, argv, i, value)) {
                return false;
            }
            out.event_services.push_back(std::move(value));
            out.events = true;
            continue;
        }
        if (arg == "--max-bytes" || arg == "--retention-bytes") {
            std::string value;
            if (!take_value(argc, argv, i, value)) {
                return false;
            }
            const auto parsed = std::strtoull(value.c_str(), nullptr, 10);
            if (arg == "--max-bytes") {
                out.max_segment_bytes = parsed;
            } else {
                out.retention_bytes = parsed;
            }
            continue;
        }
        if (std::string* target = find_value_option(value_options, arg); target != nullptr) {
            if (!take_value(argc, argv, i, *target)) {
                return false;
            }
            continue;
        }
        if (!apply_flag(flag_options, arg)) {
            return false;
        }
    }
    return out.logs || out.metrics || out.traces || out.events;
}

void usage(std::FILE* out) {
    std::fprintf(out, "usage: nexigen-recd [--session-root DIR] [--service-log NAME] "
                      "[--service-metrics NAME] [--service-traces NAME] "
                      "[--event-service NAME]... [--logs] [--metrics] [--traces] [--events] "
                      "[--max-bytes N]\n");
}

[[nodiscard]] std::string channel_topic(std::string_view stream, std::string_view app) {
    std::string topic(stream);
    topic.push_back('.');
    topic.append(app);
    return topic;
}

[[nodiscard]] std::uint32_t schema_for(std::string_view stream) noexcept {
    if (stream == "log") return 1U;
    if (stream == "metric") return 2U;
    if (stream == "trace") return 3U;
    return 1U;
}

[[nodiscard]] std::string sanitize_topic(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '-' ||
                              c == '_'
                          ? c
                          : '_');
    }
    return out;
}

class Recorder {
public:
    explicit Recorder(recorder::SessionWriter writer) : writer_{std::move(writer)} {}

    [[nodiscard]] bool initialize() {
        for (const auto& schema : recorder::builtin_schemas()) {
            if (schema.id <= 3U && !writer_.declare_schema(schema).has_value()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::uint32_t event_schema(std::size_t payload_size) {
        for (const auto& [size, id] : event_schemas_) {
            if (size == payload_size) {
                return id;
            }
        }
        const std::uint32_t id = 100U + static_cast<std::uint32_t>(event_schemas_.size());
        recorder::SchemaDeclare schema{};
        schema.id = id;
        schema.name = "nexigen.com.EventEnvelope";
        schema.encoding = "jsonschema";
        const std::size_t count = payload_size == 0U ? 1U : payload_size;
        const std::string fields = std::format(
            R"([{{"name":"sequence_id","type":"u64","offset":0,"element_size":8,"count":1,"stride":8}},)"
            R"({{"name":"timestamp_ns","type":"u64","offset":8,"element_size":8,"count":1,"stride":8}},)"
            R"({{"name":"producer_epoch","type":"u64","offset":16,"element_size":8,"count":1,"stride":8}},)"
            R"({{"name":"ingest_timestamp_ns","type":"u64","offset":24,"element_size":8,"count":1,"stride":8}},)"
            R"({{"name":"domain_epoch","type":"u64","offset":32,"element_size":8,"count":1,"stride":8}},)"
            R"({{"name":"native_ticks","type":"u64","offset":40,"element_size":8,"count":1,"stride":8}},)"
            R"({{"name":"event_code","type":"u32","offset":48,"element_size":4,"count":1,"stride":4}},)"
            R"({{"name":"hardware_event_code","type":"u32","offset":52,"element_size":4,"count":1,"stride":4}},)"
            R"({{"name":"hardware_sequence","type":"u32","offset":56,"element_size":4,"count":1,"stride":4}},)"
            R"({{"name":"domain_id","type":"u16","offset":60,"element_size":2,"count":1,"stride":2}},)"
            R"({{"name":"flags","type":"u16","offset":62,"element_size":2,"count":1,"stride":2}},)"
            R"({{"name":"payload","type":"u8[{}]","offset":64,"element_size":1,"count":{},"stride":1}}])",
            count, count);
        schema.data.assign(fields.begin(), fields.end());
        if (!writer_.declare_schema(schema).has_value()) {
            return 0U;
        }
        event_schemas_.emplace_back(payload_size, id);
        return id;
    }

    [[nodiscard]] std::uint32_t channel_for(std::string_view stream, std::string_view suffix,
                                            std::uint32_t schema_override = 0U) {
        const std::string topic = channel_topic(stream, suffix);
        for (std::size_t i = 0; i < topics_.size(); ++i) {
            if (topics_[i] == topic) {
                return channels_[i];
            }
        }
        const std::uint32_t schema = schema_override != 0U ? schema_override : schema_for(stream);
        const auto channel =
            writer_.declare_channel(topic, schema, recorder::DatasetKind::Telemetry, "nexigen.pod");
        if (!channel.has_value()) {
            return 0U;
        }
        topics_.push_back(topic);
        channels_.push_back(*channel);
        return *channel;
    }

    [[nodiscard]] bool gap(std::uint32_t channel, std::uint64_t first, std::uint64_t count,
                           recorder::GapReason reason, std::uint64_t event_ns,
                           std::uint64_t sequence) {
        recorder::GapMarkerPayload marker{};
        marker.first_lost_sequence = first;
        marker.lost_count = count;
        marker.reason = static_cast<std::uint16_t>(reason);
        return writer_
            .append(recorder::RecordType::GapMarker, channel, std::as_bytes(std::span{&marker, 1U}),
                    event_ns, sequence, 0)
            .has_value();
    }

    [[nodiscard]] bool frame(recorder::RecordType type, std::uint32_t channel,
                             std::span<const std::byte> payload, std::uint64_t event_ns,
                             std::uint64_t sequence, std::uint64_t publisher) {
        return writer_.append(type, channel, payload, event_ns, sequence, publisher).has_value();
    }

    [[nodiscard]] bool time_sync() {
        std::int64_t offset = 0;
        return !chrono::sample_utc_offset(offset).has_value() ||
               writer_.append_time_sync(offset).has_value();
    }

    [[nodiscard]] recorder::SessionWriter& writer() noexcept { return writer_; }

private:
    recorder::SessionWriter writer_;
    std::vector<std::string> topics_{};
    std::vector<std::uint32_t> channels_{};
    std::vector<std::pair<std::size_t, std::uint32_t>> event_schemas_{};
};

void drain_logs(LogSub& sub, Recorder& rec) {
    while (true) {
        const auto got = sub.receive();
        if (!got.has_value() || !got->has_value()) {
            return;
        }
        const auto& frame = (*got)->payload();
        const auto ts = static_cast<std::uint64_t>(frame.timestamp_ns);
        const std::uint32_t channel = rec.channel_for("log", std::string_view(frame.app_id, 4U));
        if (channel == 0U) {
            return;
        }
        const std::uint64_t lost = (*got)->lost_before();
        if (lost > 0U) {
            static_cast<void>(
                rec.gap(channel, ts, lost, recorder::GapReason::IoxRingOverflow, ts, ts));
        }
        if (frame.dropped_counter > 0U) {
            static_cast<void>(rec.gap(channel, 0U, frame.dropped_counter,
                                      recorder::GapReason::StreamDiscontinuity, ts, ts));
        }
        static_cast<void>(rec.frame(recorder::RecordType::LogFrame, channel,
                                    std::as_bytes(std::span{&frame, 1U}), ts, ts,
                                    (*got)->user_header().publisher_id));
    }
}

void drain_metrics(MetricSub& sub, Recorder& rec) {
    while (true) {
        const auto got = sub.receive();
        if (!got.has_value() || !got->has_value()) {
            return;
        }
        const auto& frame = (*got)->payload();
        const auto ts = static_cast<std::uint64_t>(frame.timestamp_ns);
        const std::uint32_t channel = rec.channel_for("metric", std::string_view(frame.app_id, 4U));
        if (channel == 0U) {
            return;
        }
        const std::uint64_t lost = (*got)->lost_before();
        if (lost > 0U) {
            static_cast<void>(
                rec.gap(channel, ts, lost, recorder::GapReason::IoxRingOverflow, ts, ts));
        }
        static_cast<void>(rec.frame(recorder::RecordType::MetricFrame, channel,
                                    std::as_bytes(std::span{&frame, 1U}), ts, ts,
                                    (*got)->user_header().publisher_id));
    }
}

void drain_traces(TraceSub& sub, Recorder& rec) {
    while (true) {
        const auto got = sub.receive();
        if (!got.has_value() || !got->has_value()) {
            return;
        }
        const auto& frame = (*got)->payload();
        const auto ts = static_cast<std::uint64_t>(frame.start_ns);
        const std::uint32_t channel = rec.channel_for("trace", std::string_view(frame.app_id, 4U));
        if (channel == 0U) {
            return;
        }
        const std::uint64_t lost = (*got)->lost_before();
        if (lost > 0U) {
            static_cast<void>(
                rec.gap(channel, ts, lost, recorder::GapReason::IoxRingOverflow, ts, ts));
        }
        static_cast<void>(rec.frame(recorder::RecordType::SpanFrame, channel,
                                    std::as_bytes(std::span{&frame, 1U}), ts, ts,
                                    (*got)->user_header().publisher_id));
    }
}

void drain_events(std::vector<com::RawSubscriber>& subs,
                  std::vector<recorder::EventGapTracker>& trackers, Recorder& rec) {
    for (std::size_t index = 0; index < subs.size(); ++index) {
        auto& raw = subs[index];
        while (true) {
            const auto got = raw.receive();
            if (!got.has_value() || !got->has_value()) {
                break;
            }
            const auto& sample = **got;
            if (sample.user_header.size() < sizeof(com::EventHeader)) {
                break;
            }
            com::EventHeader header{};
            std::memcpy(&header, sample.user_header.data(), sizeof(header));
            std::vector<std::byte> payload(sizeof(header) + sample.payload.size());
            std::memcpy(payload.data(), &header, sizeof(header));
            std::memcpy(payload.data() + sizeof(header), sample.payload.data(),
                        sample.payload.size());
            const std::uint32_t channel =
                rec.channel_for("event", sanitize_topic(raw.service_name()),
                                rec.event_schema(sample.payload.size()));
            if (const auto gap = trackers[index].observe(header.producer_epoch, header.sequence_id);
                gap.has_value()) {
                static_cast<void>(rec.gap(channel, gap->first_lost_sequence, gap->lost_count,
                                          recorder::GapReason::IoxRingOverflow, header.timestamp_ns,
                                          header.sequence_id));
            }
            static_cast<void>(rec.frame(recorder::RecordType::ComPayload, channel,
                                        std::span<const std::byte>(payload), header.timestamp_ns,
                                        header.sequence_id, header.producer_epoch));
        }
    }
}

[[nodiscard]] std::optional<recorder::SessionWriter> open_session(const Options& options) {
    recorder::StorageSettings settings{};
    settings.session_root = options.session_root;
    settings.session_id =
        "recd-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    settings.max_segment_bytes = options.max_segment_bytes;
    settings.retention_max_bytes = options.retention_bytes;
    char boot_text[17]{};
    const chrono::BootIdentity identity = chrono::read_boot_identity();
    settings.boot_token = std::string(chrono::format_boot_token(identity.token, boot_text));
    if (identity.uuid[0] != '\0') {
        settings.boot_uuid = std::string(identity.uuid.data());
    }
    auto writer = recorder::SessionWriter::create(settings);
    if (!writer.has_value()) {
        return std::nullopt;
    }
    return std::optional<recorder::SessionWriter>(std::move(*writer));
}

std::vector<com::RawSubscriber> open_event_subscribers(const Options& options) {
    std::vector<com::RawSubscriber> subs;
    for (const auto& service : options.event_services) {
        const auto name = com::ServiceName::create(service.c_str());
        if (!name) {
            continue;
        }
        auto raw = com::RawSubscriber::create(*name);
        if (raw.has_value()) {
            subs.push_back(std::move(*raw));
        } else {
            std::fprintf(stderr, "nexigen-recd: event service %s not available\n", service.c_str());
        }
    }
    return subs;
}

void drain_all(const Options& options, LogSub& log_sub, MetricSub& metric_sub, TraceSub& trace_sub,
               std::vector<com::RawSubscriber>& event_subs, Recorder& rec) {
    auto last_sync = std::chrono::steady_clock::now();
    std::vector<recorder::EventGapTracker> event_trackers(event_subs.size());
    while (g_stop == 0) {
        if (options.logs) drain_logs(log_sub, rec);
        if (options.metrics) drain_metrics(metric_sub, rec);
        if (options.traces) drain_traces(trace_sub, rec);
        if (options.events) drain_events(event_subs, event_trackers, rec);
        const auto now = std::chrono::steady_clock::now();
        if (now - last_sync >= std::chrono::seconds(1)) {
            static_cast<void>(rec.time_sync());
            last_sync = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

int run(const Options& options) {
    auto writer = open_session(options);
    if (!writer.has_value()) {
        std::fprintf(stderr, "nexigen-recd: cannot create session\n");
        return 1;
    }
    Recorder rec(std::move(*writer));
    if (!rec.initialize() || !rec.time_sync()) {
        std::fprintf(stderr, "nexigen-recd: cannot initialize session\n");
        return 1;
    }

    auto node = com::Node::create();
    if (!node.has_value()) {
        std::fprintf(stderr, "nexigen-recd: cannot create transport node\n");
        return 1;
    }
    const auto log_name = com::ServiceName::create(options.service_log.c_str());
    const auto metric_name = com::ServiceName::create(options.service_metrics.c_str());
    const auto trace_name = com::ServiceName::create(options.service_traces.c_str());
    if (!log_name || !metric_name || !trace_name) {
        std::fprintf(stderr, "nexigen-recd: invalid service name\n");
        return 1;
    }
    auto log_sub = LogSub::create(*node, *log_name);
    auto metric_sub = MetricSub::create(*node, *metric_name);
    auto trace_sub = TraceSub::create(*node, *trace_name);
    if (!log_sub || !metric_sub || !trace_sub) {
        std::fprintf(stderr, "nexigen-recd: cannot open frame services\n");
        return 1;
    }

    std::vector<com::RawSubscriber> event_subs = open_event_subscribers(options);
    drain_all(options, *log_sub, *metric_sub, *trace_sub, event_subs, rec);
    static_cast<void>(rec.writer().close());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Options options{};
    if (!parse_options(argc, argv, options)) {
        usage(stderr);
        return 2;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    return run(options);
}
