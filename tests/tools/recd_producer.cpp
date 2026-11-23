// Test-only producer for the nexigen-recd smoke test.
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <thread>

#include <nexigen/com/event.hpp>
#include <nexigen/com/event_channel.hpp>
#include <nexigen/com/event_producer.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/transport_header.hpp>
#include <nexigen/logging/log_frame.hpp>
#include <nexigen/logging/log_service_options.hpp>
#include <nexigen/metrics/metric_frame.hpp>
#include <nexigen/tracing/span_frame.hpp>

namespace {
struct EventPayload {
    std::uint32_t value{0};
};
static_assert(nexigen::com::concepts::EventPayload<EventPayload>);
} // namespace

namespace {
struct Endpoints {
    using LogPub =
        nexigen::com::Publisher<nexigen::logging::LogFrame, nexigen::com::TransportHeader>;
    using MetricPub =
        nexigen::com::Publisher<nexigen::metrics::MetricFrame, nexigen::com::TransportHeader>;
    using TracePub =
        nexigen::com::Publisher<nexigen::tracing::SpanFrame, nexigen::com::TransportHeader>;

    std::optional<LogPub> log{};
    std::optional<MetricPub> metric{};
    std::optional<TracePub> trace{};
    std::optional<nexigen::com::EventNotifier> log_notifier{};
    std::optional<nexigen::com::EventNotifier> metric_notifier{};
    std::optional<nexigen::com::EventNotifier> trace_notifier{};

    [[nodiscard]] static std::optional<Endpoints> open(nexigen::com::Node& node) {
        using namespace nexigen;
        const auto log_name = com::ServiceName::create("Nexigen/Log/nexigen");
        const auto metric_name = com::ServiceName::create("Nexigen/Metrics/nexigen");
        const auto trace_name = com::ServiceName::create("Nexigen/Tracing/nexigen");
        const auto log_event = com::ServiceName::create("Nexigen/Log/nexigenEvent");
        const auto metric_event = com::ServiceName::create("Nexigen/Metrics/nexigenEvent");
        const auto trace_event = com::ServiceName::create("Nexigen/Tracing/nexigenEvent");
        if (!log_name || !metric_name || !trace_name || !log_event || !metric_event ||
            !trace_event) {
            return std::nullopt;
        }
        auto log = LogPub::create(node, *log_name, logging::log_service_options());
        auto metric = MetricPub::create(node, *metric_name);
        auto trace = TracePub::create(node, *trace_name);
        auto log_notifier = com::EventNotifier::create(node, *log_event);
        auto metric_notifier = com::EventNotifier::create(node, *metric_event);
        auto trace_notifier = com::EventNotifier::create(node, *trace_event);
        if (!log || !metric || !trace || !log_notifier || !metric_notifier || !trace_notifier) {
            return std::nullopt;
        }
        Endpoints out;
        out.log.emplace(std::move(*log));
        out.metric.emplace(std::move(*metric));
        out.trace.emplace(std::move(*trace));
        out.log_notifier.emplace(std::move(*log_notifier));
        out.metric_notifier.emplace(std::move(*metric_notifier));
        out.trace_notifier.emplace(std::move(*trace_notifier));
        return out;
    }
};

template <class Publisher, class Frame>
[[nodiscard]] bool send(Publisher& publisher, nexigen::com::EventNotifier& notifier,
                        const Frame& frame, nexigen::com::TransportHeader& header) {
    if (!publisher.send_copy(frame, header).has_value()) {
        return false;
    }
    static_cast<void>(notifier.notify());
    return true;
}

[[nodiscard]] bool publish_frames(nexigen::com::Node& node, nexigen::com::TransportHeader& header) {
    using namespace nexigen;
    auto endpoints = Endpoints::open(node);
    if (!endpoints.has_value()) {
        return false;
    }

    logging::LogFrame log{};
    log.timestamp_ns = 1U;
    std::memcpy(log.app_id, "TEST", 4U);
    log.pid = 1U;
    log.log_level = core::LogLevel::Info;
    log.payload_size = 4U;
    std::memcpy(log.payload, "hello", 5U);

    metrics::MetricFrame metric{};
    std::memcpy(metric.app_id, "TEST", 4U);
    metric.pid = 1U;
    metric.timestamp_ns = 2;

    tracing::SpanFrame span{};
    std::memcpy(span.app_id, "TEST", 4U);
    span.pid = 1U;
    span.start_ns = 3U;
    span.end_ns = 4U;
    std::memcpy(span.name, "smoke", 5U);

    if (!send(*endpoints->log, *endpoints->log_notifier, log, header)) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    header.sequence = 4U;
    if (!send(*endpoints->log, *endpoints->log_notifier, log, header)) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    header.sequence = 1U;
    if (!send(*endpoints->metric, *endpoints->metric_notifier, metric, header) ||
        !send(*endpoints->trace, *endpoints->trace_notifier, span, header)) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    logging::LogFrame second = log;
    std::memcpy(second.app_id, "APP2", 4U);
    header.sequence = 5U;
    if (!send(*endpoints->log, *endpoints->log_notifier, second, header)) {
        return false;
    }
    // Keep the publishers alive while the collector drains the last samples.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return true;
}
} // namespace

int main(int argc, char** argv) {
    using namespace nexigen;
    auto node = com::Node::create();
    if (!node.has_value()) {
        return 1;
    }
    com::TransportHeader header{};
    std::memcpy(header.app_id, "TEST", 4U);
    header.pid = 1U;
    header.publisher_id = 1U;
    header.sequence = 1U;

    const auto log_name = com::ServiceName::create("Nexigen/Log/nexigen");
    const auto metric_name = com::ServiceName::create("Nexigen/Metrics/nexigen");
    const auto trace_name = com::ServiceName::create("Nexigen/Tracing/nexigen");
    if (!log_name || !metric_name || !trace_name) {
        return 1;
    }
    const auto event_name = com::ServiceName::create("Nexigen/Test/RecdEvent");
    auto event_channel = com::EventChannel<EventPayload>::create(*node, *event_name);
    if (!event_channel.has_value()) {
        return 1;
    }
    auto event_producer = event_channel->producer(std::string_view{"APP3"});
    if (!event_producer.has_value()) {
        return 1;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--hold") {
        // Keep the event service alive while the collector subscribes, then
        // publish one sample from this same process (two EventChannels on one
        // service cannot coexist).
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        if (!event_producer->publish_now(EventPayload{7U}, 1U).has_value()) {
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return 0;
    }
    if (!publish_frames(*node, header)) {
        return 1;
    }
    if (!event_producer->publish_now(EventPayload{7U}, 1U).has_value()) {
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
}
