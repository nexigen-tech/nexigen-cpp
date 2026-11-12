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

/// @brief Client-side sink configuration for `Iceoryx2MetricSink`.
///
/// Carries the publishing application's 4-byte identifier, the iceoryx2 service
/// to publish to, and the subscriber buffer depth the daemon needs to receive
/// one publish burst without discards. It is bound at sink construction, so the
/// sink itself is not deserializable; this plain value type is.
///
/// @note Spec `metrics/iceoryx2-metric-sink`, requirement "Iceoryx2 metric sink
///       configuration".
struct Iceoryx2MetricSinkConfig {
    std::string app_id{"NEXG"}; ///< 4-byte code copied into `MetricFrame.app_id`.
    std::string service_name{"nexigen/metrics/stream"}; ///< Pub-sub service (≤ 511 B).
    std::uint64_t subscriber_max_buffer_size{256};      ///< Burst depth (0 = service default 2).

    /// @brief Render the configuration as a cold-path diagnostic string.
    ///
    /// @return Text naming the app id, service, and buffer depth.
    /// @note Spec `metrics/iceoryx2-metric-sink`, requirement "Iceoryx2 metric
    ///       sink configuration"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "Iceoryx2MetricSinkConfig(app=" + app_id + ", service=" + service_name +
               ", buffer=" + std::to_string(subscriber_max_buffer_size) + ")";
    }
};

/// @brief Free diagnostic helper mirroring
///        `Iceoryx2MetricSinkConfig::to_string()`.
///
/// @param config Configuration to render.
/// @return The same text the member returns.
/// @note Spec `metrics/iceoryx2-metric-sink`, requirement "Iceoryx2 metric sink
///       configuration"; diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const Iceoryx2MetricSinkConfig& config) {
    return config.to_string();
}

/// @brief Write the sink configuration as JSON (cold path).
///
/// @param j Output JSON object with the app id, service name, and buffer depth.
/// @param config Configuration to serialize.
/// @note Spec `metrics/iceoryx2-metric-sink`, requirement "Iceoryx2 metric sink
///       configuration"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const Iceoryx2MetricSinkConfig& config);
/// @brief Parse a sink configuration from JSON without throwing.
///
/// Values are assembled into a default-constructed configuration first, so
/// fields that are missing or wrongly typed keep their defaults; the assembled
/// value then replaces `out`.
///
/// @param j JSON object to read.
/// @param out Destination, replaced by the assembled configuration.
/// @note Spec `metrics/iceoryx2-metric-sink`, requirement "Iceoryx2 metric sink
///       configuration".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, Iceoryx2MetricSinkConfig& out) noexcept;

} // namespace nexigen::metrics

/// @brief `std::formatter` for `Iceoryx2MetricSinkConfig`.
///
/// @note Project mandate 11; formatting is cold path only.
template <>
struct std::formatter<nexigen::metrics::Iceoryx2MetricSinkConfig>
    : std::formatter<std::string_view> {
    /// @brief Format the configuration through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param config Configuration to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::metrics::Iceoryx2MetricSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
