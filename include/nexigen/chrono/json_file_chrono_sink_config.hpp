// nexigen::chrono — JSON-lines file sink configuration (change
// add-chrono-clock-service; spec chrono/chrono-sink "JSON-file chrono sink").
//
// The file sink's configuration value type: the path and the append flag. The
// sink itself lives in json_file_chrono_sink.hpp.
#pragma once

#include <format>
#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::chrono {

/// File sink configuration: the path and whether snapshots are appended.
struct JsonFileChronoSinkConfig {
    std::string path{};
    bool append{true};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "JsonFileChronoSinkConfig(path=" + path + ", append=" + (append ? "true" : "false") +
               ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const JsonFileChronoSinkConfig& config) {
    return config.to_string();
}

/// Configuration JSON (cold path).
inline void to_json(nlohmann::json& j, const JsonFileChronoSinkConfig& config) {
    j = nlohmann::json{{"path", config.path}, {"append", config.append}};
}

inline void from_json(const nlohmann::json& j, JsonFileChronoSinkConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto it = j.find("path");
    if (it != j.end() && it->is_string()) {
        const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
        if (text != nullptr) {
            out.path = std::string(*text);
        }
    }
    const auto append_it = j.find("append");
    if (append_it != j.end() && append_it->is_boolean()) {
        out.append = append_it->get<bool>();
    }
}

} // namespace nexigen::chrono

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::chrono::JsonFileChronoSinkConfig>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::JsonFileChronoSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
