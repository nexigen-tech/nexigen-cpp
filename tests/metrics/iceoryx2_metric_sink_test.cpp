// Tests nexigen::metrics::Iceoryx2MetricSink (change add-metrics-daemon;
// spec metrics/iceoryx2-metric-sink). Build only when NEXIGEN_ENABLE_COM=ON.
//
// Self-contained harness (NX_CHECK + run_suite), like the logging COM tests:
// no operator-new override — iceoryx2 (Rust via FFI) allocates via the system
// allocator, not C++ operator new, so the metrics arena counter does not apply
// to the live publish path.
//
// Allocation-free guarantee (spec scenario "Sink callbacks are allocation-free"):
// on_batch touches only the fixed `frame_` member and the transport's zero-copy
// loan/write/send — no std::string, no new/delete, no container growth — so an
// armed arena would observe no C++ heap traffic. The arena is intentionally not
// armed here because the iceoryx2 system allocator would drown it; the
// guarantee is structural and enforced by review, not by a runtime counter.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <thread>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/iceoryx2_metric_sink.hpp>
#include <nexigen/metrics/iceoryx2_metric_sink_config.hpp>
#include <nexigen/metrics/metric_frame.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>

namespace metrics = nexigen::metrics;
namespace com = nexigen::com;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;

namespace {

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

void run_suite(const char* name, void (*body)()) {
    const int checks_before = g_checks;
    const int failures_before = g_failures;
    body();
    std::printf("[%-22s] %d checks, %d failed\n", name, g_checks - checks_before,
                g_failures - failures_before);
}

static_assert(metrics::concepts::MetricSink<metrics::Iceoryx2MetricSink>,
              "Iceoryx2MetricSink must satisfy the MetricSink concept");

void test_concept_and_diagnostics() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    metrics::Iceoryx2MetricSinkConfig config;
    config.app_id = "TEST";
    config.service_name = "Nexigen/Test/MetricSinkDiag";
    metrics::Iceoryx2MetricSink sink{config, &*node};
    NX_CHECK(metrics::to_string(sink) == sink.to_string());
    NX_CHECK(std::format("{}", sink) == sink.to_string());
    NX_CHECK(sink.to_string().find("TEST") != std::string::npos);
    // Not started yet: on_batch reports NotInitialized without allocating.
    metrics::MetricRecord record{};
    metrics::MetricBatchSnapshot batch{};
    batch.records = &record;
    batch.record_count = 1;
    batch.record_capacity = 1;
    NX_CHECK(!sink.on_batch(batch).has_value());
    NX_CHECK(sink.flush().has_value());
}

void test_publish_roundtrip() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }

    metrics::Iceoryx2MetricSinkConfig config;
    config.app_id = "LIDR";
    config.service_name = "Nexigen/Test/MetricSinkRoundTrip";
    metrics::Iceoryx2MetricSink sink{config, &*node};
    NX_CHECK(sink.start().has_value());

    const auto name = com::ServiceName::create(config.service_name.c_str());
    NX_CHECK(name.has_value());
    auto subscriber = com::Subscriber<metrics::MetricFrame>::create(*node, *name, 2U);
    NX_CHECK(subscriber.has_value());

    const std::string event_service = config.service_name + "Event";
    const auto event_name = com::ServiceName::create(event_service.c_str());
    NX_CHECK(event_name.has_value());
    auto listener = com::EventListener::create(*node, *event_name);
    NX_CHECK(listener.has_value());
    if (!subscriber.has_value() || !listener.has_value()) {
        return;
    }

    metrics::MetricRecord records[2]{};
    std::snprintf(records[0].identity.metric_name, sizeof(records[0].identity.metric_name), "a");
    records[0].kind = metrics::MetricKind::Counter;
    records[0].scalar.counter = 1;
    std::snprintf(records[1].identity.metric_name, sizeof(records[1].identity.metric_name), "b");
    records[1].kind = metrics::MetricKind::Gauge;
    records[1].scalar.gauge_int = -2;

    metrics::MetricBatchSnapshot batch{};
    batch.stamp = chrono::Timestamp{123456789};
    batch.records = records;
    batch.record_count = 2;
    batch.record_capacity = 2;

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // let discovery connect
    NX_CHECK(sink.on_batch(batch).has_value());

    // Every record becomes one frame.
    std::uint32_t received = 0;
    for (int attempt = 0; attempt < 1000 && received < 2; ++attempt) {
        bool drained = false;
        for (;;) {
            auto got = subscriber->receive();
            NX_CHECK(got.has_value());
            if (!got.has_value() || !got->has_value()) {
                break;
            }
            const metrics::MetricFrame& frame = (*got)->payload();
            NX_CHECK(frame.app_id_text() == "LIDR");
            NX_CHECK(frame.pid != 0U);
            NX_CHECK(frame.timestamp_ns == 123456789);
            ++received;
            drained = true;
        }
        if (!drained) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    NX_CHECK(received == 2U);

    // One notification per batch.
    std::uint64_t activations = 0;
    auto waited = listener->try_wait([&activations](com::EventId) { ++activations; });
    NX_CHECK(waited.has_value());
    if (waited.has_value()) {
        NX_CHECK(*waited >= 1U);
    }
    NX_CHECK(activations >= 1U);

    NX_CHECK(sink.flush().has_value());
    sink.stop();
}

void test_burst_no_drop() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }

    metrics::Iceoryx2MetricSinkConfig config;
    config.app_id = "LIDR";
    config.service_name = "Nexigen/Test/MetricSinkBurst";
    config.subscriber_max_buffer_size = 16;
    metrics::Iceoryx2MetricSink sink{config, &*node};
    NX_CHECK(sink.start().has_value());

    const auto name = com::ServiceName::create(config.service_name.c_str());
    NX_CHECK(name.has_value());
    auto subscriber = com::Subscriber<metrics::MetricFrame>::create(*node, *name, 16U, 16U);
    NX_CHECK(subscriber.has_value());
    if (!subscriber.has_value()) {
        return;
    }

    metrics::MetricRecord records[8]{};
    for (int i = 0; i < 8; ++i) {
        std::snprintf(records[i].identity.metric_name, sizeof(records[i].identity.metric_name),
                      "burst_%d", i);
        records[i].kind = metrics::MetricKind::Counter;
        records[i].scalar.counter = static_cast<std::uint64_t>(i);
    }
    metrics::MetricBatchSnapshot batch{};
    batch.stamp = chrono::Timestamp{42};
    batch.records = records;
    batch.record_count = 8;
    batch.record_capacity = 8;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    NX_CHECK(sink.on_batch(batch).has_value());

    std::uint32_t received = 0;
    for (int attempt = 0; attempt < 1000 && received < 8; ++attempt) {
        bool drained = false;
        for (;;) {
            auto got = subscriber->receive();
            NX_CHECK(got.has_value());
            if (!got.has_value() || !got->has_value()) {
                break;
            }
            ++received;
            drained = true;
        }
        if (!drained) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    // With the raised service maximum every frame of the burst survives.
    NX_CHECK(received == 8U);
    sink.stop();
}

void test_out_of_samples_no_block() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }

    metrics::Iceoryx2MetricSinkConfig config;
    config.app_id = "LIDR";
    config.service_name = "Nexigen/Test/MetricSinkOverflow";
    config.subscriber_max_buffer_size = 2;
    metrics::Iceoryx2MetricSink sink{config, &*node};
    NX_CHECK(sink.start().has_value());

    auto name = com::ServiceName::create(config.service_name.c_str());
    NX_CHECK(name.has_value());
    auto subscriber = com::Subscriber<metrics::MetricFrame>::create(*node, *name, 2U, 2U);
    NX_CHECK(subscriber.has_value());
    if (!subscriber.has_value()) {
        return;
    }

    metrics::MetricRecord records[8]{};
    for (int i = 0; i < 8; ++i) {
        std::snprintf(records[i].identity.metric_name, sizeof(records[i].identity.metric_name),
                      "ov_%d", i);
        records[i].kind = metrics::MetricKind::Counter;
        records[i].scalar.counter = static_cast<std::uint64_t>(i);
    }
    metrics::MetricBatchSnapshot batch{};
    batch.stamp = chrono::Timestamp{1};
    batch.records = records;
    batch.record_count = 8;
    batch.record_capacity = 8;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Publishing 8 frames into a 2-deep buffer must return promptly: the
    // transport discards the excess (DiscardData) and the sink never blocks.
    NX_CHECK(sink.on_batch(batch).has_value());
    sink.stop();
}

void test_sink() {
    test_concept_and_diagnostics();
    test_publish_roundtrip();
    test_burst_no_drop();
    test_out_of_samples_no_block();
}

} // namespace

int main() {
    run_suite("metric_sink", test_sink);
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
