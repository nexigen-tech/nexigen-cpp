// nexigen::tracing — ChromeTraceSinkConfig (change
// refactor-hub-model-config-headers; extracted verbatim from
// chrome_trace_sink.hpp).
#pragma once

#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::tracing {

/// Chrome sink configuration (value type, full serialization trio; body in
/// src/tracing/sink_format.cpp).
struct ChromeTraceSinkConfig {
    std::string path{}; ///< the trace file to (re)create at start().

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const ChromeTraceSinkConfig& config);

/// ADL JSON (cold path, full round-trip); bodies in sink_format.cpp.
void to_json(nlohmann::json& j, const ChromeTraceSinkConfig& config);
void from_json(const nlohmann::json& j, ChromeTraceSinkConfig& out) noexcept;

} // namespace nexigen::tracing

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::tracing::ChromeTraceSinkConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::ChromeTraceSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
