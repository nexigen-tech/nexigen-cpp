// nexigen::metrics — MetricDaemonConfig (change add-metrics-daemon;
// spec metrics/metrics-daemon "MetricsDaemon configuration" / diagnostics).

#include "metrics_test_support.hpp"

#include <nexigen/metrics/metric_daemon_config.hpp>

namespace {

void test_metric_daemon_config_roundtrip() {
    metrics::MetricDaemonConfig config;
    config.service_name = "nexigen/metrics/stream";
    config.bind_address = "0.0.0.0";
    config.port = 9200;
    config.endpoint = "/metrics";
    config.enable_http = true;
    config.storage_directory = "/tmp/metrics_history";
    config.max_storage_mb = 64;
    config.batch_flush_interval_ms = 250;
    config.max_metrics = 128;

    const nlohmann::json j = config;
    NX_CHECK(j.is_object());
    metrics::MetricDaemonConfig parsed;
    metrics::from_json(j, parsed);
    NX_CHECK(parsed.to_string() == config.to_string());
    NX_CHECK(parsed.port == 9200);
    NX_CHECK(parsed.max_metrics == 128U);

    NX_CHECK(metrics::to_string(config) == config.to_string());
    NX_CHECK(std::format("{}", config) == config.to_string());
    NX_CHECK(config.validate().has_value());
}

void test_metric_daemon_config_validation() {
    metrics::MetricDaemonConfig base;

    auto empty_service = base;
    empty_service.service_name.clear();
    NX_CHECK(!empty_service.validate().has_value());

    auto relative_storage = base;
    relative_storage.storage_directory = "relative/dir";
    NX_CHECK(!relative_storage.validate().has_value());

    auto empty_host = base;
    empty_host.bind_address.clear();
    NX_CHECK(!empty_host.validate().has_value());

    auto bad_endpoint = base;
    bad_endpoint.endpoint = "metrics";
    NX_CHECK(!bad_endpoint.validate().has_value());
}

void test_metric_daemon_config() {
    test_metric_daemon_config_roundtrip();
    test_metric_daemon_config_validation();
}

} // namespace

void suite_metric_daemon_config() {
    run_suite("metric_daemon_config", test_metric_daemon_config);
}
