// nexigen::metrics — cold-path JSON serialization for MetricDaemonConfig
// (spec metrics/metrics-daemon; change refactor-complexity-hotspots).
//
// Strictly non-throwing under `JSON_NOEXCEPTION=1`: the object is parsed into a
// local and assigned only on success. `port` keeps its documented clamp to the
// 16-bit range instead of being rejected.
// nlohmann/json headers trigger -Wnull-dereference under -O3 (pre-existing
// upstream issue). Suppress for this TU only — the JSON code is cold-path.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <cstdint>
#include <string>
#include <utility>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/metrics/metric_daemon_config.hpp>

namespace nexigen::metrics {

namespace {

// HTTP port: values above 65535 are clamped, not rejected (pre-refactor rule).
void parse_port(const nlohmann::json& j, std::uint16_t& port) noexcept {
    std::uint32_t raw = 0U;
    if (!core::detail::read_unsigned(j, "port", raw)) {
        return;
    }
    port = static_cast<std::uint16_t>(raw > 65535U ? 65535U : raw);
}

} // namespace

void to_json(nlohmann::json& j, const MetricDaemonConfig& config) {
    j = nlohmann::json{
        {"service_name", config.service_name},
        {"bind_address", config.bind_address},
        {"port", config.port},
        {"endpoint", config.endpoint},
        {"enable_http", config.enable_http},
        {"storage_directory", config.storage_directory},
        {"max_storage_mb", config.max_storage_mb},
        {"batch_flush_interval_ms", config.batch_flush_interval_ms},
        {"max_metrics", config.max_metrics},
        {"bg_cpu_affinity", config.bg_cpu_affinity},
    };
}

void from_json(const nlohmann::json& j, MetricDaemonConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    MetricDaemonConfig parsed{};
    core::detail::read_string(j, "service_name", parsed.service_name);
    core::detail::read_string(j, "bind_address", parsed.bind_address);
    core::detail::read_string(j, "endpoint", parsed.endpoint);
    core::detail::read_string(j, "storage_directory", parsed.storage_directory);
    core::detail::read_unsigned(j, "max_storage_mb", parsed.max_storage_mb);
    core::detail::read_unsigned(j, "batch_flush_interval_ms", parsed.batch_flush_interval_ms);
    core::detail::read_unsigned(j, "max_metrics", parsed.max_metrics);
    core::detail::read_bool(j, "enable_http", parsed.enable_http);
    parse_port(j, parsed.port);
    core::detail::read_object(j, "bg_cpu_affinity",
                              [&parsed](const nlohmann::json& affinity) noexcept {
                                  from_json(affinity, parsed.bg_cpu_affinity);
                              });
    out = std::move(parsed);
}

} // namespace nexigen::metrics
