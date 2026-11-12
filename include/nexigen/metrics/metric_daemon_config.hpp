// nexigen::metrics — MetricsDaemon construction-time configuration (change
// add-metrics-daemon; spec metrics/metrics-daemon "MetricsDaemon
// configuration"; design D8).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>

namespace nexigen::metrics {

/// MetricsDaemon construction-time configuration. Validated at startup; invalid
/// values are rejected before any thread, subscriber, or listener is created.
struct MetricDaemonConfig {
    std::string service_name{"nexigen/metrics/stream"};        ///< subscription (≤ 511 B)
    std::string bind_address{"127.0.0.1"};                     ///< HTTP bind host
    std::uint16_t port{9100};                                  ///< HTTP port
    std::string endpoint{"/metrics"};                          ///< Prometheus scrape path
    bool enable_http{true};                                    ///< serve the scrape endpoint
    std::string storage_directory{"/var/log/nexigen/metrics"}; ///< absolute history root
    std::uint64_t max_storage_mb{50};                          ///< history quota (0 = unbounded)
    std::uint32_t batch_flush_interval_ms{500};                ///< render/flush timer (ms)
    std::uint32_t max_metrics{256};                            ///< aggregation table capacity
    core::ThreadSpec bg_cpu_affinity{};                        ///< drain worker affinity
    core::ThreadSpec http_thread{};                            ///< HTTP thread (empty = bg)

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        std::string out = "MetricDaemonConfig(service=" + service_name;
        out += ", bind=" + bind_address + ":" + std::to_string(port);
        out += ", endpoint=" + endpoint;
        out += ", http=" + std::string(enable_http ? "true" : "false");
        out += ", storage=" + storage_directory;
        out += ", max_storage_mb=" + std::to_string(max_storage_mb);
        out += ", flush_interval_ms=" + std::to_string(batch_flush_interval_ms);
        out += ", max_metrics=" + std::to_string(max_metrics);
        out += ", bg_affinity=" + bg_cpu_affinity.to_string();
        out += ", http_thread=" + http_thread.to_string() + ")";
        return out;
    }

    /// Startup validation. Rejects empty/oversized names, an empty host, a
    /// relative endpoint, and a non-absolute storage directory.
    [[nodiscard]] core::Status validate() const noexcept {
        if (service_name.empty() || service_name.size() > 511U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (bind_address.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (endpoint.empty() || endpoint.front() != '/') {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (storage_directory.empty() || storage_directory.front() != '/' ||
            storage_directory.size() > 511U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (http_thread.has_requests()) {
            const core::Status valid = http_thread.validate();
            if (!valid) {
                return valid;
            }
        }
        return {};
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const MetricDaemonConfig& config) {
    return config.to_string();
}

/// Cold-path JSON round-trip (mandate 10), hostile-safe under JSON_NOEXCEPTION=1;
/// bodies live in src/metrics/metric_daemon_config.cpp.
void to_json(nlohmann::json& j, const MetricDaemonConfig& config);
void from_json(const nlohmann::json& j, MetricDaemonConfig& out) noexcept;

} // namespace nexigen::metrics

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::MetricDaemonConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::MetricDaemonConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
