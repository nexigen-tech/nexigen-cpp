#pragma once

#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/null_logger.hpp>
#include <nexigen/logging/settings.hpp>
#include <nexigen/metrics/metrics_settings.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>
#include <nexigen/tracing/null_trace_sink.hpp>
#include <nexigen/tracing/tracing_settings.hpp>

namespace nexigen::app::policies {

/// @brief Infrastructure policy with no observable output.
///
/// Useful for benchmarks, unit tests and shadow runs: the call shape stays
/// identical, but every logger/metric/trace record is discarded.
struct NullPolicy {
    using Logger = logging::NullLogger;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;

    static core::Status emplace_logger(std::optional<Logger>& out, const logging::Settings&,
                                       std::string_view) noexcept {
        out.emplace();
        return {};
    }

    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&, std::string_view) noexcept {
        out.emplace();
        return {};
    }

    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           std::string_view) noexcept {
        out.emplace();
        return {};
    }

    /// @brief Render the stateless policy for cold-path diagnostics.
    /// @return The literal `NullPolicy()`.
    [[nodiscard]] std::string to_string() const { return "NullPolicy()"; }
};

/// @brief Free diagnostic helper mirroring `NullPolicy::to_string`.
[[nodiscard]] inline std::string to_string(const NullPolicy& policy) {
    return policy.to_string();
}

/// @brief Serialize the policy identity for cold-path diagnostics.
inline void to_json(nlohmann::json& j, const NullPolicy&) {
    j = nlohmann::json{{"type", "NullPolicy"}};
}

/// @brief Stateless policy: JSON input carries no state to restore.
inline void from_json(const nlohmann::json&, NullPolicy&) noexcept {}

} // namespace nexigen::app::policies

/// @brief `std::formatter` for the null policy.
template <>
struct std::formatter<nexigen::app::policies::NullPolicy> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::app::policies::NullPolicy& policy, Ctx& ctx) const {
        const std::string text = policy.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
