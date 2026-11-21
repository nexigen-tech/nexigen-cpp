// nexigen-recd configuration serialization tests.
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "nexigen-recd/recorder_daemon_config.hpp"

namespace {
int g_failures = 0;
#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #__VA_ARGS__);                      \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)
} // namespace

int main() {
    using nexigen::recorder::RecorderDaemonConfig;
    RecorderDaemonConfig config{};
    config.session_root = "/tmp/sessions";
    config.service_log = "Log";
    config.service_metrics = "Metrics";
    config.service_traces = "Traces";
    config.event_services = {"EventA", "EventB"};
    config.logs = true;
    config.metrics = false;
    config.traces = true;
    config.events = true;
    config.max_segment_bytes = 4096U;
    config.retention_bytes = 8192U;

    nlohmann::json encoded = config;
    RecorderDaemonConfig decoded{};
    nlohmann::json::parse(encoded.dump()).get_to(decoded);
    CHECK(decoded.session_root == config.session_root);
    CHECK(decoded.service_log == config.service_log);
    CHECK(decoded.service_metrics == config.service_metrics);
    CHECK(decoded.service_traces == config.service_traces);
    CHECK(decoded.event_services == config.event_services);
    CHECK(decoded.logs == config.logs);
    CHECK(decoded.metrics == config.metrics);
    CHECK(decoded.traces == config.traces);
    CHECK(decoded.events == config.events);
    CHECK(decoded.max_segment_bytes == config.max_segment_bytes);
    CHECK(decoded.retention_bytes == config.retention_bytes);

    RecorderDaemonConfig hostile = decoded;
    const nlohmann::json bad = {{"session_root", 1}, {"logs", "yes"}, {"retention_bytes", -1}};
    nexigen::recorder::from_json(bad, hostile);
    CHECK(hostile.session_root == decoded.session_root);
    CHECK(hostile.logs == decoded.logs);
    CHECK(hostile.retention_bytes == decoded.retention_bytes);

    const std::string text = config.to_string();
    CHECK(text.find("RecorderDaemonConfig") != std::string::npos);
    CHECK(std::format("{}", config).find("Log") != std::string::npos);

    std::printf("recd config: %d failures\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
