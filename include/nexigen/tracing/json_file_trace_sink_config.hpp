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

/// File sink configuration (value type, full serialization trio; body in
/// src/tracing/sink_format.cpp).
struct JsonFileTraceSinkConfig {
    std::string path{};     ///< file (None) or filename prefix.
    std::string app_name{}; ///< application name for the "app" field.
    RotationPolicy rotation{RotationPolicy::None};
    std::uint64_t max_spans_per_window{0}; ///< 0 = unlimited.
    std::uint64_t window_ns{0};            ///< 0 = unlimited.

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const JsonFileTraceSinkConfig& config);

/// ADL JSON (cold path, full round-trip under JSON_NOEXCEPTION=1); bodies in
/// src/tracing/sink_format.cpp.
void to_json(nlohmann::json& j, const JsonFileTraceSinkConfig& config);
void from_json(const nlohmann::json& j, JsonFileTraceSinkConfig& out) noexcept;

} // namespace nexigen::tracing

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::tracing::JsonFileTraceSinkConfig>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::JsonFileTraceSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
