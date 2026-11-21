// Test-only producer for the nexigen-recd smoke test.
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#include <nexigen/com/event_channel.hpp>
#include <nexigen/com/event_producer.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/transport_header.hpp>
#include <nexigen/logging/log_frame.hpp>
#include <nexigen/metrics/metric_frame.hpp>
#include <nexigen/tracing/span_frame.hpp>

namespace {
struct EventPayload {
    std::uint32_t value{0};
};
static_assert(nexigen::com::concepts::EventPayload<EventPayload>);
} // namespace

namespace {
[[nodiscard]] bool publish_frames(nexigen::com::Node& node,
                                 nexigen::com::TransportHeader& header) {
    using namespace nexigen;
    const auto log_name = com::ServiceName::create("Nexigen/Log/nexigen");
    const auto metric_name = com::ServiceName::create("Nexigen/Metrics/nexigen");
    const auto trace_name = com::ServiceName::create("Nexigen/Tracing/nexigen");
    if (!log_name || !metric_name || !trace_name) {
        return false;
    }
    auto log_pub = com::Publisher<logging::LogFrame, com::TransportHeader>::create(node, *log_name);
    auto metric_pub =
        com::Publisher<metrics::MetricFrame, com::TransportHeader>::create(node, *metric_name);
    auto trace_pub =
        com::Publisher<tracing::SpanFrame, com::TransportHeader>::create(node, *trace_name);
    if (!log_pub || !metric_pub || !trace_pub) {
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

    if (!log_pub->send_copy(log, header).has_value()) {
        return false;
    }
    header.sequence = 4U;
    if (!log_pub->send_copy(log, header).has_value()) {
        return false;
    }
    header.sequence = 1U;
    if (!metric_pub->send_copy(metric, header).has_value()) {
        return false;
    }
    if (!trace_pub->send_copy(span, header).has_value()) {
        return false;
    }
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
    auto event_producer = event_channel->producer();
    if (!event_producer.has_value()) {
        return 1;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--hold") {
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
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
