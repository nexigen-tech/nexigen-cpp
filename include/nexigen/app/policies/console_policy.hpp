#pragma once

#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/console_logger.hpp>
#include <nexigen/logging/settings.hpp>
#include <nexigen/metrics/console_metric_sink.hpp>
#include <nexigen/metrics/metrics_settings.hpp>
#include <nexigen/tracing/console_trace_sink.hpp>
#include <nexigen/tracing/tracing_settings.hpp>

namespace nexigen::app::policies {

/// @brief Infrastructure policy that writes all telemetry to the console.
///
/// Logs go to fd 1/2 by severity, metric batches and spans to their console
/// sinks. Suitable for examples, smoke runs and interactive debugging.
struct ConsolePolicy {
    using Logger = logging::ConsoleLogger<>;
    using MetricsSink = metrics::ConsoleMetricSink;
    using TraceSink = tracing::ConsoleTraceSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Settings& settings,
                                       std::string_view /*app_name*/) noexcept {
        logging::ConsoleLoggerConfig config{};
        const core::Status applied = logging::apply_to(settings, config);
        if (!applied) {
            return applied;
        }
        out.emplace();
        return out->initialize(config);
    }

    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&,
                                             std::string_view /*app_name*/) noexcept {
        out.emplace();
        return {};
    }

    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           std::string_view /*app_name*/) noexcept {
        out.emplace(1);
        return {};
    }

    /// @brief Render the stateless policy for cold-path diagnostics.
    /// @return The literal `ConsolePolicy()`.
    [[nodiscard]] std::string to_string() const { return "ConsolePolicy()"; }
};

/// @brief Free diagnostic helper mirroring `ConsolePolicy::to_string`.
[[nodiscard]] inline std::string to_string(const ConsolePolicy& policy) {
    return policy.to_string();
}

/// @brief Serialize the policy identity for cold-path diagnostics.
inline void to_json(nlohmann::json& j, const ConsolePolicy&) {
    j = nlohmann::json{{"type", "ConsolePolicy"}};
}

/// @brief Stateless policy: JSON input carries no state to restore.
inline void from_json(const nlohmann::json&, ConsolePolicy&) noexcept {}

} // namespace nexigen::app::policies

/// @brief `std::formatter` for the console policy.
template <>
struct std::formatter<nexigen::app::policies::ConsolePolicy> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::app::policies::ConsolePolicy& policy, Ctx& ctx) const {
        const std::string text = policy.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
