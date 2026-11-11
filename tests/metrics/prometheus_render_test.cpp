// nexigen::metrics — Prometheus text exposition renderer (change
// add-metrics-daemon; spec metrics/metrics-daemon).

#include "metrics_test_support.hpp"

#include <cstring>
#include <string>

#include <nexigen/metrics/detail/prometheus_render.hpp>
#include <nexigen/metrics/metric_frame.hpp>

namespace {

metrics::MetricFrame make_frame(const char* metric_name) {
    metrics::MetricFrame frame{};
    frame.app_id[0] = 'L';
    frame.app_id[1] = 'I';
    frame.app_id[2] = 'D';
    frame.app_id[3] = 'R';
    frame.pid = 42;
    std::snprintf(frame.record.identity.meter_name, sizeof(frame.record.identity.meter_name),
                  "perception");
    std::snprintf(frame.record.identity.meter_version, sizeof(frame.record.identity.meter_version),
                  "1.0");
    std::snprintf(frame.record.identity.metric_name, sizeof(frame.record.identity.metric_name),
                  "%s", metric_name);
    return frame;
}

std::string render(const metrics::MetricFrame& frame) {
    char buffer[8192];
    const std::size_t n = metrics::detail::render_prometheus(frame, buffer, 0U, sizeof(buffer));
    return std::string(buffer, n);
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

void test_prometheus_counter() {
    auto frame = make_frame("requests_total");
    frame.record.kind = metrics::MetricKind::Counter;
    frame.record.scalar.counter = 7;
    const std::string text = render(frame);
    NX_CHECK(contains(text, "# TYPE requests_total counter\n"));
    NX_CHECK(contains(
        text, "requests_total{app=\"LIDR\",meter=\"perception\",version=\"1.0\",pid=\"42\"} 7\n"));
    // No per-sample timestamp suffix: the output is exactly the TYPE line plus
    // one sample line ending in the value and a newline.
    NX_CHECK(text ==
             "# TYPE requests_total counter\n"
             "requests_total{app=\"LIDR\",meter=\"perception\",version=\"1.0\",pid=\"42\"} 7\n");
}

void test_prometheus_gauge() {
    auto frame = make_frame("temperature");
    std::snprintf(frame.record.identity.unit, sizeof(frame.record.identity.unit), "celsius");
    frame.record.kind = metrics::MetricKind::Gauge;
    frame.record.scalar.is_double = false;
    frame.record.scalar.gauge_int = -11;
    const std::string text = render(frame);
    NX_CHECK(contains(text, "# TYPE temperature gauge\n"));
    NX_CHECK(contains(text, "# UNIT temperature celsius\n"));
    NX_CHECK(contains(
        text, "temperature{app=\"LIDR\",meter=\"perception\",version=\"1.0\",pid=\"42\"} -11\n"));
}

void test_prometheus_descriptive_statistics() {
    auto frame = make_frame("latency");
    frame.record.kind = metrics::MetricKind::DescriptiveStatistics;
    frame.record.statistics.count = 5;
    frame.record.statistics.mean = 2.5;
    frame.record.statistics.stddev = 1.5;
    frame.record.statistics.min = 1.0;
    frame.record.statistics.max = 4.0;
    const std::string text = render(frame);
    NX_CHECK(contains(text, "# TYPE latency_mean gauge\n"));
    NX_CHECK(contains(
        text, "latency_mean{app=\"LIDR\",meter=\"perception\",version=\"1.0\",pid=\"42\"} 2.5\n"));
    NX_CHECK(contains(
        text,
        "latency_stddev{app=\"LIDR\",meter=\"perception\",version=\"1.0\",pid=\"42\"} 1.5\n"));
    NX_CHECK(contains(
        text, "latency_min{app=\"LIDR\",meter=\"perception\",version=\"1.0\",pid=\"42\"} 1\n"));
    NX_CHECK(contains(
        text, "latency_max{app=\"LIDR\",meter=\"perception\",version=\"1.0\",pid=\"42\"} 4\n"));
    NX_CHECK(contains(
        text, "latency_count{app=\"LIDR\",meter=\"perception\",version=\"1.0\",pid=\"42\"} 5\n"));
}

void test_prometheus_histogram() {
    auto frame = make_frame("request_ms");
    frame.record.kind = metrics::MetricKind::ExplicitHistogram;
    frame.record.histogram.bucket_count = 4;
    frame.record.histogram.buckets_lower[0] = 0.0;
    frame.record.histogram.buckets_lower[1] = 1.0;
    frame.record.histogram.buckets_lower[2] = 2.0;
    frame.record.histogram.buckets_lower[3] = 10.0;
    frame.record.histogram.buckets_count[0] = 1;
    frame.record.histogram.buckets_count[1] = 2;
    frame.record.histogram.buckets_count[2] = 3;
    frame.record.histogram.buckets_count[3] = 4;
    frame.record.histogram.count = 10;
    frame.record.histogram.sum = 20.0;
    const std::string text = render(frame);
    NX_CHECK(contains(text, "# TYPE request_ms histogram\n"));
    NX_CHECK(contains(text, "request_ms_bucket{app=\"LIDR\",meter=\"perception\",version=\"1.0\","
                            "pid=\"42\",le=\"1\"} 1\n"));
    NX_CHECK(contains(text, "request_ms_bucket{app=\"LIDR\",meter=\"perception\",version=\"1.0\","
                            "pid=\"42\",le=\"2\"} 3\n"));
    NX_CHECK(contains(text, "request_ms_bucket{app=\"LIDR\",meter=\"perception\",version=\"1.0\","
                            "pid=\"42\",le=\"10\"} 6\n"));
    NX_CHECK(contains(text, "request_ms_bucket{app=\"LIDR\",meter=\"perception\",version=\"1.0\","
                            "pid=\"42\",le=\"+Inf\"} 10\n"));
    NX_CHECK(contains(
        text, "request_ms_sum{app=\"LIDR\",meter=\"perception\",version=\"1.0\",pid=\"42\"} 20\n"));
    NX_CHECK(contains(
        text,
        "request_ms_count{app=\"LIDR\",meter=\"perception\",version=\"1.0\",pid=\"42\"} 10\n"));
}

void test_prometheus_state() {
    auto frame = make_frame("app_mode_state");
    frame.record.kind = metrics::MetricKind::State;
    std::snprintf(frame.record.state.value, sizeof(frame.record.state.value), "CALIBRATING");
    const std::string text = render(frame);
    NX_CHECK(contains(text, "# TYPE app_mode_state gauge\n"));
    NX_CHECK(contains(text, "app_mode_state{app=\"LIDR\",meter=\"perception\",version=\"1.0\",pid="
                            "\"42\",state=\"CALIBRATING\"} 1\n"));
}

void test_prometheus_overflow() {
    auto frame = make_frame("x");
    frame.record.kind = metrics::MetricKind::Counter;
    char small[8];
    const std::size_t n = metrics::detail::render_prometheus(frame, small, 0U, sizeof(small));
    NX_CHECK(n == 0U); // did not fit: caller keeps its previous length
}

void test_prometheus_render() {
    test_prometheus_counter();
    test_prometheus_gauge();
    test_prometheus_descriptive_statistics();
    test_prometheus_histogram();
    test_prometheus_state();
    test_prometheus_overflow();
}

} // namespace

void suite_prometheus_render() {
    run_suite("prometheus_render", test_prometheus_render);
}
