// nexigen::chrono — JSON-lines file sink configuration (change
// add-chrono-clock-service; spec chrono/chrono-sink "JSON-file chrono sink").
//
// The file sink's configuration value type: the path and the append flag. The
// sink itself lives in json_file_sink.hpp.
#pragma once

#include <format>
#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::chrono {

/// File sink configuration: the path and whether snapshots are appended.
struct JsonFileSinkConfig {
    std::string path{};
    bool append{true};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "JsonFileSinkConfig(path=" + path + ", append=" + (append ? "true" : "false") + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const JsonFileSinkConfig& config) {
    return config.to_string();
}

/// Configuration JSON (cold path).
/// Declarations: bodies live in json_file_sink_config.cpp.
void to_json(nlohmann::json& j, const JsonFileSinkConfig& config);
void from_json(const nlohmann::json& j, JsonFileSinkConfig& out) noexcept;

} // namespace nexigen::chrono

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::chrono::JsonFileSinkConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::JsonFileSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
