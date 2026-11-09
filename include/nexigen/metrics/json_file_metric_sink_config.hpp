// nexigen::metrics — JsonFileMetricSinkConfig (change
// refactor-hub-model-config-headers; extracted verbatim from
// json_file_metric_sink.hpp).
#pragma once

#include <format>
#include <string>
#include <string_view>

namespace nexigen::metrics {

/// File sink configuration: the path and whether batches are appended.
struct JsonFileMetricSinkConfig {
    std::string path{};
    std::string app_name{};
    bool append{true};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "JsonFileMetricSinkConfig(path=" + path + ", app_name=" + app_name +
               ", append=" + (append ? "true" : "false") + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const JsonFileMetricSinkConfig& config) {
    return config.to_string();
}

} // namespace nexigen::metrics

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::JsonFileMetricSinkConfig>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::JsonFileMetricSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
