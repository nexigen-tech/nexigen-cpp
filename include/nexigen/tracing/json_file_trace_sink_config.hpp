// nexigen::tracing — JsonFileTraceSinkConfig (change
// refactor-hub-model-config-headers; extracted verbatim from
// json_file_trace_sink.hpp).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/tracing/rotation_policy.hpp>

namespace nexigen::tracing {

/// @brief Configuration of the rotating, rate-limited JSONL trace sink.
///
/// A plain value type consumed when a `JsonFileTraceSink` is constructed: the
/// target path or prefix, the application name stamped on every line, the
/// rotation schedule, and the optional per-window span budget. Two zeros for
/// `max_spans_per_window`/`window_ns` mean unlimited. An empty `path` means "no
/// explicit sink configured" for the settings loader.
///
/// @note Spec `tracing/trace-sinks`, requirements "Sink configuration
///       serialization" and "Trace spans carry the application name".
struct JsonFileTraceSinkConfig {
    std::string path{};                            ///< file (None) or filename prefix.
    std::string app_name{};                        ///< application name for the "app" field.
    RotationPolicy rotation{RotationPolicy::None}; ///< Window schedule; None keeps one file.
    std::uint64_t max_spans_per_window{0};         ///< 0 = unlimited.
    std::uint64_t window_ns{0};                    ///< 0 = unlimited.

    /// @brief Render the configuration as a cold-path diagnostic string.
    ///
    /// @return Text naming the path, application name, rotation policy, and
    ///         rate-limit fields.
    /// @note Project mandate 9 (`to_string`); diagnostic only.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring
///        `JsonFileTraceSinkConfig::to_string()`.
///
/// @param config Configuration to render.
/// @return The same text the member returns.
/// @note Project mandate 9 (`to_string`); cold path only.
[[nodiscard]] std::string to_string(const JsonFileTraceSinkConfig& config);

/// @brief Serialize the configuration as JSON (cold path).
///
/// @param j Destination JSON value, overwritten with every configuration field.
/// @param config Configuration to serialize.
/// @note Spec `tracing/trace-sinks`, requirement "Sink configuration
///       serialization"; project mandate 10 (JSON).
void to_json(nlohmann::json& j, const JsonFileTraceSinkConfig& config);

/// @brief Parse the configuration from JSON without throwing.
///
/// Round-trips fully under `JSON_NOEXCEPTION=1`; missing or wrongly typed
/// fields keep their prior value and a hostile document never aborts.
///
/// @param j JSON object to read.
/// @param out Destination configuration, updated field by field.
/// @note Spec `tracing/trace-sinks`, requirement "Sink configuration
///       serialization"; project mandate 10 (JSON).
void from_json(const nlohmann::json& j, JsonFileTraceSinkConfig& out) noexcept;

} // namespace nexigen::tracing

/// @brief `std::formatter` for `JsonFileTraceSinkConfig`.
///
/// @note Project mandate 11 (`std::formatter`); formatting is cold path only.
template <>
struct std::formatter<nexigen::tracing::JsonFileTraceSinkConfig>
    : std::formatter<std::string_view> {
    /// @brief Format the configuration through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param config Configuration to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::JsonFileTraceSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
