// nexigen::metrics — MetricFrame wire type (change add-metrics-daemon;
// spec metrics/iceoryx2-metric-sink "MetricFrame wire type").

#include "metrics_test_support.hpp"

#include <cstddef>

#include <nexigen/metrics/metric_frame.hpp>

namespace {

void test_metric_frame_layout() {
    static_assert(std::is_trivially_copyable_v<metrics::MetricFrame>);
    // The frame is app_id + pid + timestamp_ns + one record, naturally aligned.
    NX_CHECK(sizeof(metrics::MetricFrame) ==
             offsetof(metrics::MetricFrame, record) + sizeof(metrics::MetricRecord));
    NX_CHECK(offsetof(metrics::MetricFrame, record) == 16U);
    NX_CHECK(alignof(metrics::MetricFrame) == alignof(metrics::MetricRecord));
}

void test_metric_frame_serialization() {
    metrics::MetricFrame frame{};
    frame.app_id[0] = 'L';
    frame.app_id[1] = 'I';
    frame.app_id[2] = 'D';
    frame.app_id[3] = 'R';
    frame.pid = 42;
    frame.timestamp_ns = 123456789;
    std::snprintf(frame.record.identity.metric_name, sizeof(frame.record.identity.metric_name),
                  "latency_us");
    frame.record.kind = metrics::MetricKind::Counter;
    frame.record.scalar.counter = 7;

    NX_CHECK(frame.app_id_text() == "LIDR");

    const nlohmann::json j = frame;
    NX_CHECK(j.is_object());
    NX_CHECK(j.at("app_id").get<std::string>() == "LIDR");
    NX_CHECK(j.at("pid").get<std::uint32_t>() == 42U);

    metrics::MetricFrame parsed{};
    metrics::from_json(j, parsed);
    NX_CHECK(parsed.app_id_text() == "LIDR");
    NX_CHECK(parsed.pid == 42U);
    NX_CHECK(parsed.timestamp_ns == 123456789);
    NX_CHECK(parsed.record.kind == metrics::MetricKind::Counter);
    NX_CHECK(parsed.record.scalar.counter == 7U);
    NX_CHECK(metrics::detail::view_name(parsed.record.identity.metric_name) == "latency_us");
}

void test_metric_frame_diagnostics() {
    metrics::MetricFrame frame{};
    frame.app_id[0] = 'N';
    frame.app_id[1] = 'X';
    frame.pid = 7;
    std::snprintf(frame.record.identity.metric_name, sizeof(frame.record.identity.metric_name),
                  "m");
    NX_CHECK(!frame.to_string().empty());
    NX_CHECK(metrics::to_string(frame) == frame.to_string());
    NX_CHECK(std::format("{}", frame) == frame.to_string());
}

void test_metric_frame() {
    test_metric_frame_layout();
    test_metric_frame_serialization();
    test_metric_frame_diagnostics();
}

} // namespace

void suite_metric_frame() {
    run_suite("metric_frame", test_metric_frame);
}
