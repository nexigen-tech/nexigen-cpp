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

/// @brief `MetricsDaemon` construction-time configuration.
///
/// Validated at startup; invalid values are rejected before any thread,
/// subscriber, or listener is created. The type is a plain value aggregate; the
/// daemon copies it and never mutates the caller's instance.
///
/// @note Spec `metrics/metrics-daemon`, requirement "MetricsDaemon
///       configuration".
/// @note Spec `metrics/metrics-daemon`, requirement "MetricsDaemon HTTP thread
///       is configurable".
struct MetricDaemonConfig {
    std::string service_name{"nexigen/metrics/stream"};        ///< Subscription service (≤ 511 B).
    std::string bind_address{"127.0.0.1"};                     ///< HTTP bind host.
    std::uint16_t port{9100};                                  ///< HTTP port.
    std::string endpoint{"/metrics"};                          ///< Prometheus scrape path.
    bool enable_http{true};                                    ///< Serve the scrape endpoint.
    std::string storage_directory{"/var/log/nexigen/metrics"}; ///< Absolute history root.
    std::uint64_t max_storage_mb{50};                          ///< History quota (0 = unbounded).
    std::uint32_t batch_flush_interval_ms{500};                ///< Render/flush timer (ms).
    std::uint32_t max_metrics{256};                            ///< Aggregation table capacity.
    core::ThreadSpec bg_cpu_affinity{};                        ///< Drain worker affinity.
    core::ThreadSpec http_thread{}; ///< HTTP thread (empty = use `bg_cpu_affinity`).

    /// @brief Render the configuration as a cold-path diagnostic string.
    ///
    /// @return Text naming the service, endpoint, storage directory, and thread
    ///         specs.
    /// @note Spec `metrics/metrics-daemon`, requirement "MetricsDaemon
    ///       diagnostics"; diagnostic only (project mandate 9).
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

    /// @brief Validate the fields the daemon needs before constructing anything.
    ///
    /// Rejects an empty or oversized service name, an empty bind host, an
    /// endpoint not starting with `/`, a storage directory that is empty,
    /// relative, or oversized, and a non-empty `http_thread` spec that fails its
    /// own validation.
    ///
    /// @return `Ok` when the configuration is usable; otherwise the error arm
    ///         carries `core::ErrorCode::InvalidArgument` (or the propagated
    ///         `http_thread` failure).
    /// @note Spec `metrics/metrics-daemon`, requirement "MetricsDaemon
    ///       configuration".
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

/// @brief Free diagnostic helper mirroring `MetricDaemonConfig::to_string()`.
///
/// @param config Configuration to render.
/// @return The same text the member returns.
/// @note Spec `metrics/metrics-daemon`, requirement "MetricsDaemon
///       diagnostics"; diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const MetricDaemonConfig& config) {
    return config.to_string();
}

/// @brief Write the daemon configuration as JSON (cold path).
///
/// @param j Output JSON object with every visible field, including both thread
///        specs.
/// @param config Configuration to serialize.
/// @note Spec `metrics/metrics-daemon`, requirement "MetricsDaemon diagnostics";
///       JSON only (project mandate 10).
void to_json(nlohmann::json& j, const MetricDaemonConfig& config);
/// @brief Parse a daemon configuration from JSON without throwing.
///
/// Values are assembled into a default-constructed configuration first, so
/// fields that are missing or wrongly typed keep their defaults; the assembled
/// value then replaces `out`.
///
/// @param j JSON object to read.
/// @param out Destination, replaced by the assembled configuration.
/// @note Spec `metrics/metrics-daemon`, requirement "MetricsDaemon diagnostics".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, MetricDaemonConfig& out) noexcept;

} // namespace nexigen::metrics

/// @brief `std::formatter` for `MetricDaemonConfig`, rendering its diagnostics.
///
/// @note Project mandate 11; formatting is cold path only.
template <>
struct std::formatter<nexigen::metrics::MetricDaemonConfig> : std::formatter<std::string_view> {
    /// @brief Format the configuration through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param config Configuration to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::metrics::MetricDaemonConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
