// nexigen::tracing — ChromeTraceSinkConfig (change
// refactor-hub-model-config-headers; extracted verbatim from
// chrome_trace_sink.hpp).
#pragma once

#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::tracing {

/// @brief Configuration of the Chrome Trace Event sink.
///
/// A plain value type consumed once when a `ChromeTraceSink` is constructed;
/// it carries only the target path. Full serialization trio: member and free
/// `to_string`, non-throwing JSON round-trip, and `std::formatter`.
///
/// @note Spec `tracing/trace-sinks`, requirement "Sink configuration
///       serialization".
struct ChromeTraceSinkConfig {
    std::string path{}; ///< Trace file created (truncated) at the sink's `start()`.

    /// @brief Render the configuration as a cold-path diagnostic string.
    ///
    /// @return Text naming the configured trace path.
    /// @note Project mandate 9 (`to_string`); diagnostic only.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `ChromeTraceSinkConfig::to_string()`.
///
/// @param config Configuration to render.
/// @return The same text the member returns.
/// @note Project mandate 9 (`to_string`); cold path only.
[[nodiscard]] std::string to_string(const ChromeTraceSinkConfig& config);

/// @brief Serialize the configuration as JSON (cold path).
///
/// @param j Destination JSON value, overwritten with the configuration fields.
/// @param config Configuration to serialize.
/// @note Spec `tracing/trace-sinks`, requirement "Sink configuration
///       serialization"; project mandate 10 (JSON).
void to_json(nlohmann::json& j, const ChromeTraceSinkConfig& config);

/// @brief Parse the configuration from JSON without throwing.
///
/// Fields that are missing or wrongly typed keep their prior value, so a
/// hostile document never aborts under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON object to read.
/// @param out Destination configuration, updated field by field.
/// @note Spec `tracing/trace-sinks`, requirement "Sink configuration
///       serialization"; project mandate 10 (JSON).
void from_json(const nlohmann::json& j, ChromeTraceSinkConfig& out) noexcept;

} // namespace nexigen::tracing

/// @brief `std::formatter` for `ChromeTraceSinkConfig`.
///
/// @note Project mandate 11 (`std::formatter`); formatting is cold path only.
template <>
struct std::formatter<nexigen::tracing::ChromeTraceSinkConfig> : std::formatter<std::string_view> {
    /// @brief Format the configuration through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param config Configuration to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::ChromeTraceSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
