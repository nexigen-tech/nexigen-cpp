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

/// @brief Configuration value for the JSON-lines file sink.
///
/// Carries only the target path and the append policy; opening and I/O live in
/// `JsonFileSink`. A default-constructed value has an empty path, which the
/// settings loader treats as "no explicit sink configured" and never reports as
/// a diagnostic.
///
/// @note Spec `chrono/chrono-sink`, requirement "File sink configuration
///       serialization"; spec `chrono/chrono-settings`, requirement "Chrono
///       settings carry a sink configuration".
struct JsonFileSinkConfig {
    std::string path{}; ///< Output file path; empty means unconfigured.
    bool append{true};  ///< Append to an existing file instead of truncating it.

    /// @brief Render this configuration as a diagnostic string.
    ///
    /// @return Text of the form `JsonFileSinkConfig(path=..., append=...)`.
    /// @note Spec `chrono/chrono-sink`, requirement "File sink configuration
    ///       serialization"; project mandate 9.
    [[nodiscard]] std::string to_string() const {
        return "JsonFileSinkConfig(path=" + path + ", append=" + (append ? "true" : "false") + ")";
    }
};

/// @brief Free-helper mirror of `JsonFileSinkConfig::to_string`.
///
/// @param config Configuration to render.
/// @return The same text as `config.to_string()`.
/// @note Spec `chrono/chrono-sink`, requirement "File sink configuration
///       serialization"; project mandate 9.
[[nodiscard]] inline std::string to_string(const JsonFileSinkConfig& config) {
    return config.to_string();
}

/// @brief Serialize the sink configuration as JSON (cold path).
///
/// The body lives in `src/chrono/json_file_sink_config.cpp`.
///
/// @param j Destination JSON value, replaced by `path` and `append` fields.
/// @param config Configuration to serialize.
/// @note Spec `chrono/chrono-sink`, requirement "File sink configuration
///       serialization"; project mandate 10.
void to_json(nlohmann::json& j, const JsonFileSinkConfig& config);
/// @brief Parse the sink configuration from JSON, strictly non-throwing.
///
/// Under `JSON_NOEXCEPTION=1` a non-object value leaves `out` unchanged, and a
/// missing or mistyped field is skipped so that field keeps its previous value.
/// The body lives in `src/chrono/json_file_sink_config.cpp`.
///
/// @param j Source JSON value.
/// @param out Destination configuration; `path` and `append` update in place.
/// @note Spec `chrono/chrono-sink`, requirement "File sink configuration
///       serialization"; project mandate 10.
void from_json(const nlohmann::json& j, JsonFileSinkConfig& out) noexcept;

} // namespace nexigen::chrono

/// @brief `std::formatter` specialization rendering the configuration diagnostically.
///
/// @note Spec `chrono/chrono-sink`, requirement "File sink configuration
///       serialization"; project mandate 11.
template <>
struct std::formatter<nexigen::chrono::JsonFileSinkConfig> : std::formatter<std::string_view> {
    /// @brief Format a sink configuration as its diagnostic string.
    ///
    /// @tparam Ctx Format context type; the inherited string specifiers apply.
    /// @param config Configuration to format.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    /// @note Project mandate 11 (`std::formatter`).
    template <class Ctx>
    auto format(const nexigen::chrono::JsonFileSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
