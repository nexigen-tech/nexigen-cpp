// nexigen::metrics — JsonFileMetricSinkConfig (change
// refactor-hub-model-config-headers; extracted verbatim from
// json_file_metric_sink.hpp).
#pragma once

#include <format>
#include <string>
#include <string_view>

namespace nexigen::metrics {

/// @brief File sink configuration: target path, app name, and append mode.
///
/// A plain value type carried by `metrics::Settings` and consumed when a
/// `JsonFileMetricSink` is constructed; an empty `path` means "no explicit sink
/// configured". The loader reads only `path` and `append` from JSON, while
/// `app_name` is populated by the wiring code.
///
/// @note Spec `metrics/metric-sinks`, requirement "File sink configuration
///       serialization".
/// @note Spec `metrics/metrics-settings`, requirement "Sink object reads the
///       file-sink keys".
struct JsonFileMetricSinkConfig {
    std::string path{};     ///< Output JSON-lines file; empty disables the sink.
    std::string app_name{}; ///< Application name stamped on batches.
    bool append{true};      ///< Append to an existing file instead of truncating.

    /// @brief Render the configuration as a cold-path diagnostic string.
    ///
    /// @return Text naming the path, app name, and append flag.
    /// @note Spec `metrics/metric-sinks`, requirement "File sink configuration
    ///       serialization"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "JsonFileMetricSinkConfig(path=" + path + ", app_name=" + app_name +
               ", append=" + (append ? "true" : "false") + ")";
    }
};

/// @brief Free diagnostic helper mirroring
///        `JsonFileMetricSinkConfig::to_string()`.
///
/// @param config Configuration to render.
/// @return The same text the member returns.
/// @note Spec `metrics/metric-sinks`, requirement "File sink configuration
///       serialization"; diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const JsonFileMetricSinkConfig& config) {
    return config.to_string();
}

} // namespace nexigen::metrics

/// @brief `std::formatter` for `JsonFileMetricSinkConfig`.
///
/// @note Project mandate 11; formatting is cold path only.
template <>
struct std::formatter<nexigen::metrics::JsonFileMetricSinkConfig>
    : std::formatter<std::string_view> {
    /// @brief Format the configuration through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param config Configuration to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::metrics::JsonFileMetricSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
