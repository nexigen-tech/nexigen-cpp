// nexigen::metrics — Iceoryx2MetricSinkConfig (change add-metrics-daemon;
// spec metrics/iceoryx2-metric-sink "Iceoryx2 metric sink configuration").

#include "metrics_test_support.hpp"

#include <nexigen/metrics/iceoryx2_metric_sink_config.hpp>

namespace {

void test_iceoryx2_metric_sink_config() {
    metrics::Iceoryx2MetricSinkConfig config;
    config.app_id = "LIDR";
    config.service_name = "nexigen/metrics/stream";
    config.subscriber_max_buffer_size = 8;

    const nlohmann::json j = config;
    NX_CHECK(j.is_object());
    metrics::Iceoryx2MetricSinkConfig parsed;
    metrics::from_json(j, parsed);
    NX_CHECK(parsed.app_id == "LIDR");
    NX_CHECK(parsed.service_name == "nexigen/metrics/stream");
    NX_CHECK(parsed.subscriber_max_buffer_size == 8U);

    NX_CHECK(metrics::to_string(config) == config.to_string());
    NX_CHECK(std::format("{}", config) == config.to_string());
    NX_CHECK(config.to_string().find("LIDR") != std::string::npos);
}

} // namespace

void suite_iceoryx2_metric_sink_config() {
    run_suite("iceoryx2_metric_sink_config", test_iceoryx2_metric_sink_config);
}
