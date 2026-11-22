#pragma once

#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/app/telemetry_identity.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/logging/settings.hpp>
#include <nexigen/metrics/json_file_metric_sink.hpp>
#include <nexigen/metrics/metrics_settings.hpp>
#include <nexigen/tracing/json_file_trace_sink.hpp>
#include <nexigen/tracing/tracing_settings.hpp>

namespace nexigen::app::policies {

/// @brief Infrastructure policy that writes all telemetry to JSONL files.
///
/// The logger uses `logging::Settings::options.log_root_directory` (hourly
/// rotation by default); metric and trace sinks use the `sink` objects from
/// their settings, with `app_name` stamped on every record. The application
/// name comes from the infrastructure, not from the logging JSON.
struct FilePolicy {
    using Logger = logging::FileLogger<>;            ///< Rotating JSONL file logger.
    using MetricsSink = metrics::JsonFileMetricSink; ///< JSONL metric-batch sink.
    using TraceSink = tracing::JsonFileTraceSink;    ///< JSONL span sink.

    /// @brief Construct and initialize the rotating file logger.
    ///
    /// @param out Optional storage filled on success.
    /// @param settings Logging settings, including the log root directory.
    /// @param identity Run identity supplying `app_name` for every record.
    /// @return Success, or a capacity/configuration/initialization failure.
    ///
    /// @note Spec `app/policies`, requirement "Stateless infrastructure policy".
    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Settings& settings,
                                       const TelemetryIdentity& identity) noexcept {
        const core::Status capacity =
            logging::check_capacity<Logger::capacity(), Logger::max_threads(), 128>(settings);
        if (!capacity) {
            return capacity;
        }
        logging::FileLoggerConfig config{};
        const core::Status applied = logging::apply_to(settings, config);
        if (!applied) {
            return applied;
        }
        config.app_name = std::string(identity.app_name);
        out.emplace();
        return out->initialize(config);
    }

    /// @brief Construct the JSONL metric sink.
    ///
    /// @param out Optional storage filled on success.
    /// @param settings Metrics settings whose `sink.path` must be non-empty.
    /// @param identity Run identity supplying `app_name` for every record.
    /// @return Success, or `InvalidArgument` when the sink path is empty.
    ///
    /// @note Spec `app/policies`, requirement "Stateless infrastructure policy".
    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings& settings,
                                             const TelemetryIdentity& identity) noexcept {
        if (settings.sink.path.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        metrics::JsonFileMetricSinkConfig config = settings.sink;
        config.app_name = std::string(identity.app_name);
        out.emplace(config);
        return {};
    }

    /// @brief Construct the JSONL trace sink.
    ///
    /// @param out Optional storage filled on success.
    /// @param settings Tracing settings whose `sink.path` must be non-empty.
    /// @param identity Run identity supplying `app_name` for every record.
    /// @return Success, or `InvalidArgument` when the sink path is empty.
    ///
    /// @note Spec `app/policies`, requirement "Stateless infrastructure policy".
    static core::Status emplace_trace_sink(std::optional<TraceSink>& out,
                                           const tracing::Settings& settings,
                                           const TelemetryIdentity& identity) noexcept {
        if (settings.sink.path.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        tracing::JsonFileTraceSinkConfig config = settings.sink;
        config.app_name = std::string(identity.app_name);
        out.emplace(config);
        return {};
    }

    /// @brief Render the stateless policy for cold-path diagnostics.
    /// @return The literal `FilePolicy()`.
    [[nodiscard]] std::string to_string() const { return "FilePolicy()"; }
};

/// @brief Free diagnostic helper mirroring `FilePolicy::to_string`.
[[nodiscard]] inline std::string to_string(const FilePolicy& policy) {
    return policy.to_string();
}

/// @brief Serialize the policy identity for cold-path diagnostics.
///
/// @param j Output JSON object receiving `{"type":"FilePolicy"}`.
/// @param policy Stateless policy; carries no fields.
inline void to_json(nlohmann::json& j, const FilePolicy&) {
    j = nlohmann::json{{"type", "FilePolicy"}};
}

/// @brief Stateless policy: JSON input carries no state to restore.
///
/// @param j Input JSON; ignored.
/// @param policy Policy left unchanged.
inline void from_json(const nlohmann::json&, FilePolicy&) noexcept {}

} // namespace nexigen::app::policies

/// @brief `std::formatter` for the file policy.
template <>
struct std::formatter<nexigen::app::policies::FilePolicy> : std::formatter<std::string_view> {
    /// @brief Format the policy through `to_string`.
    ///
    /// @tparam Ctx Format context type.
    /// @param policy Policy to render.
    /// @param ctx Format context receiving the text.
    /// @return Iterator past the last emitted character.
    template <class Ctx>
    auto format(const nexigen::app::policies::FilePolicy& policy, Ctx& ctx) const {
        const std::string text = policy.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
