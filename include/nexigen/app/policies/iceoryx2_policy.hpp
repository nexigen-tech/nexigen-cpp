#pragma once

// This header requires a COM-enabled build: link nexigen::com,
// nexigen::logging_com, nexigen::metrics_com and nexigen::tracing_com.

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <nexigen/com/node.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/iceoryx2_logger.hpp>
#include <nexigen/logging/settings.hpp>
#include <nexigen/metrics/iceoryx2_metric_sink.hpp>
#include <nexigen/metrics/metrics_settings.hpp>
#include <nexigen/tracing/iceoryx2_trace_sink.hpp>
#include <nexigen/tracing/tracing_settings.hpp>

namespace nexigen::app::policies {

/// @brief Process-wide node borrowed by the metric and trace sinks.
///
/// `Iceoryx2Logger` owns its own node; the metric and trace sinks take a
/// borrowed node, so the policy keeps one shared node alive for the process.
/// The node is created lazily on the first call and is intentionally never
/// destroyed before process exit: sinks and providers may outlive `main`'s
/// stack objects only up to the `Infrastructure` destructor, which is earlier.
[[nodiscard]] inline com::Node* iceoryx2_shared_node() noexcept {
    static std::optional<com::Node> node = []() -> std::optional<com::Node> {
        auto created = com::Node::create();
        if (!created.has_value()) {
            return std::nullopt;
        }
        return std::optional<com::Node>{std::move(*created)};
    }();
    return node.has_value() ? &*node : nullptr;
}

/// @brief Default node provider: the process-wide shared iceoryx2 node.
///
/// The policy template accepts any provider with a static `get()` returning a
/// borrowed `com::Node*`. The default keeps the production singleton; tests can
/// inject a failing provider to exercise the `SystemError` path.
struct SharedNodeProvider {
    [[nodiscard]] static com::Node* get() noexcept { return iceoryx2_shared_node(); }
};

/// @brief Infrastructure policy that publishes all telemetry to iceoryx2.
///
/// Logs become `LogFrame`s on `Nexigen/Log/nexigen`; metric batches and spans
/// become `MetricFrame`/`SpanFrame`s on `nexigen/metrics/stream` and
/// `nexigen/trace/stream`. The four template characters are the application
/// tag copied into every frame, so a daemon can attribute a record to one
/// executable.
///
/// @tparam A First tag character.
/// @tparam B Second tag character.
/// @tparam C Third tag character.
/// @tparam D Fourth tag character.
/// @tparam NodeProvider Node source with a static `get()`; defaults to the
///         process-wide shared node. A failing provider drives the
///         `SystemError` path in tests.
/// @note Requires `NEXIGEN_ENABLE_COM=ON` and the `*_com` link targets.
template <char A, char B, char C, char D, class NodeProvider = SharedNodeProvider>
struct Iceoryx2Policy {
    using Logger = logging::Iceoryx2Logger<>;
    using MetricsSink = metrics::Iceoryx2MetricSink;
    using TraceSink = tracing::Iceoryx2TraceSink;

    /// @brief Four-character application tag plus NUL.
    static constexpr char k_tag[5] = {A, B, C, D, '\0'};

    [[nodiscard]] static std::string app_id() { return std::string(k_tag, 4U); }

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Settings& settings,
                                       std::string_view /*app_name*/) noexcept {
        logging::Iceoryx2LoggerConfig config{};
        config.app_id = app_id();
        config.initial_level = settings.options.initial_log_level;
        config.worker_thread = settings.options.worker_thread;
        config.worker_poll_interval_us = settings.options.worker_poll_interval_us;
        out.emplace();
        return out->initialize(config);
    }

    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&,
                                             std::string_view /*app_name*/) noexcept {
        com::Node* node = NodeProvider::get();
        if (node == nullptr) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        metrics::Iceoryx2MetricSinkConfig config{};
        config.service_name = "nexigen/metrics/stream";
        config.app_id = app_id();
        out.emplace(config, node);
        return out->start();
    }

    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           std::string_view /*app_name*/) noexcept {
        com::Node* node = NodeProvider::get();
        if (node == nullptr) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        tracing::Iceoryx2TraceSinkConfig config{};
        config.service_name = "nexigen/trace/stream";
        config.app_id = app_id();
        out.emplace(config, node);
        return {};
    }

    /// @brief Render the policy and its application tag for diagnostics.
    /// @return Text of the form `Iceoryx2Policy(app_id=WAIT)`.
    [[nodiscard]] std::string to_string() const {
        return "Iceoryx2Policy(app_id=" + std::string(k_tag, 4U) + ")";
    }
};

/// @brief Free diagnostic helper mirroring `Iceoryx2Policy::to_string`.
template <char A, char B, char C, char D, class NodeProvider>
[[nodiscard]] inline std::string to_string(const Iceoryx2Policy<A, B, C, D, NodeProvider>& policy) {
    return policy.to_string();
}

/// @brief Serialize the policy identity and application tag.
template <char A, char B, char C, char D, class NodeProvider>
inline void to_json(nlohmann::json& j, const Iceoryx2Policy<A, B, C, D, NodeProvider>&) {
    j = nlohmann::json{
        {"type", "Iceoryx2Policy"},
        {"app_id", std::string(Iceoryx2Policy<A, B, C, D, NodeProvider>::k_tag, 4U)}};
}

/// @brief Stateless policy: JSON input carries no state to restore.
template <char A, char B, char C, char D, class NodeProvider>
inline void from_json(const nlohmann::json&, Iceoryx2Policy<A, B, C, D, NodeProvider>&) noexcept {}

} // namespace nexigen::app::policies

/// @brief `std::formatter` for the iceoryx2 policy.
template <char A, char B, char C, char D, class NodeProvider>
struct std::formatter<nexigen::app::policies::Iceoryx2Policy<A, B, C, D, NodeProvider>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::app::policies::Iceoryx2Policy<A, B, C, D, NodeProvider>& policy,
                Ctx& ctx) const {
        const std::string text = policy.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
