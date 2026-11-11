// nexigen::metrics — Iceoryx2MetricSink construction-time configuration
// (change add-metrics-daemon; spec metrics/iceoryx2-metric-sink "Iceoryx2
// metric sink configuration").
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::metrics {

/// Client-side sink configuration: the publishing app's 4-byte identifier, the
/// iceoryx2 service to publish to, and the subscriber buffer depth the daemon
/// needs to receive one publish burst without discards.
struct Iceoryx2MetricSinkConfig {
    std::string app_id{"NEXG"};                         ///< 4-byte code into MetricFrame.app_id
    std::string service_name{"nexigen/metrics/stream"}; ///< pub-sub service (≤ 511 B)
    std::uint64_t subscriber_max_buffer_size{256};      ///< burst depth (0 = service default 2)

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "Iceoryx2MetricSinkConfig(app=" + app_id + ", service=" + service_name +
               ", buffer=" + std::to_string(subscriber_max_buffer_size) + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const Iceoryx2MetricSinkConfig& config) {
    return config.to_string();
}

/// Cold-path JSON round-trip (mandate 10), hostile-safe under JSON_NOEXCEPTION=1.
/// Declarations: bodies live in iceoryx2_metric_sink_config.cpp.
void to_json(nlohmann::json& j, const Iceoryx2MetricSinkConfig& config);
void from_json(const nlohmann::json& j, Iceoryx2MetricSinkConfig& out) noexcept;

} // namespace nexigen::metrics

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::Iceoryx2MetricSinkConfig>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::Iceoryx2MetricSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
