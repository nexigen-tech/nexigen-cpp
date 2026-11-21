// nexigen::com + nexigen::metrics — endpoint counters reach a metric sink.
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nexigen/com/event_channel.hpp>
#include <nexigen/com/event_producer.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/transport_header.hpp>
#include <nexigen/metrics/metric_batch_snapshot.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/meter.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #__VA_ARGS__);                      \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

struct EventPayload {
    std::uint32_t value{0};
};

struct CaptureSink {
    std::vector<std::string> names{};
    std::vector<std::uint64_t> values{};

    [[nodiscard]] nexigen::core::Status
    on_event(const nexigen::metrics::MetricBatchSnapshot& batch) noexcept {
        for (std::uint32_t i = 0; i < batch.record_count; ++i) {
            names.emplace_back(batch.records[i].identity.metric_name);
            values.push_back(batch.records[i].scalar.counter);
        }
        return {};
    }
    [[nodiscard]] nexigen::core::Status flush() noexcept { return {}; }
};

[[nodiscard]] bool has_counter(const CaptureSink& sink, std::string_view name,
                               std::uint64_t value) {
    for (std::size_t i = 0; i < sink.names.size(); ++i) {
        if (sink.names[i] == name && sink.values[i] == value) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace {

bool test_event_counters(nexigen::metrics::MetricProvider& provider, CaptureSink& sink,
                         nexigen::com::Node& node) {
    using namespace nexigen;
    auto event_pub_meter = provider.get_meter("com.event.publisher", "1");
    auto event_sub_meter = provider.get_meter("com.event.consumer", "1");
    CHECK(event_pub_meter.has_value());
    CHECK(event_sub_meter.has_value());
    if (!event_pub_meter.has_value() || !event_sub_meter.has_value()) {
        return 1;
    }
    const auto event_name = com::ServiceName::create("Nexigen/Test/EndpointCountersEvent");
    auto channel = com::EventChannel<EventPayload>::create(node, *event_name);
    CHECK(channel.has_value());
    if (!channel.has_value()) {
        return 1;
    }
    auto event_producer = channel->producer(**event_pub_meter);
    CHECK(event_producer.has_value());
    if (!event_producer.has_value()) {
        return 1;
    }
    auto consumer = channel->consumer([](const com::EventHeader&, const EventPayload&) {},
                                      [](const com::LossInfo&) {}, **event_sub_meter);
    CHECK(consumer.has_value());
    if (!consumer.has_value()) {
        return 1;
    }
    CHECK(event_producer->publish_now(EventPayload{1U}, 1U).has_value());
    for (int attempt = 0; attempt < 100; ++attempt) {
        CHECK(consumer->drain().has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(provider.collect_now().has_value());
    CHECK(has_counter(sink, "published", 1U));
    CHECK(has_counter(sink, "received", 1U));

    return true;
}

} // namespace

int main() {
    using namespace nexigen;
    auto node = com::Node::create();
    CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/EndpointCounters");
    CHECK(name.has_value());
    if (!node.has_value() || !name.has_value()) {
        return 1;
    }

    CaptureSink sink{};
    metrics::MetricProviderConfig config{};
    config.max_meters = 8;
    config.max_counters = 16;
    metrics::MetricProvider provider{config, sink};

    auto pub_meter = provider.get_meter("com.publisher", "1");
    auto sub_meter = provider.get_meter("com.subscriber", "1");
    CHECK(pub_meter.has_value());
    CHECK(sub_meter.has_value());
    if (!pub_meter.has_value() || !sub_meter.has_value()) {
        return 1;
    }

    com::PublishSubscribeOptions options{};
    options.subscriber_max_buffer_size = 8U;
    options.subscriber_buffer_size = 8U;
    auto publisher = com::Publisher<int, com::TransportHeader, metrics::Meter>::create(
        *node, *name, options, *pub_meter);
    auto subscriber = com::Subscriber<int, com::TransportHeader, metrics::Meter>::create(
        *node, *name, options, *sub_meter);
    CHECK(publisher.has_value());
    CHECK(subscriber.has_value());
    if (!publisher.has_value() || !subscriber.has_value()) {
        return 1;
    }

    com::TransportHeader header{};
    for (std::uint64_t i = 1; i <= 3U; ++i) {
        header.sequence = i;
        CHECK(publisher->send_copy(1, header).has_value());
    }
    for (int attempt = 0; attempt < 200; ++attempt) {
        const auto got = subscriber->receive();
        CHECK(got.has_value());
        if (got.has_value() && got->has_value()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(provider.collect_now().has_value());
    CHECK(has_counter(sink, "published", 3U));
    CHECK(has_counter(sink, "received", 1U));

    CHECK(test_event_counters(provider, sink, *node));
    std::printf("endpoint counters: %zu records, %d failures\n", sink.names.size(), g_failures);
    return g_failures == 0 ? 0 : 1;
}
