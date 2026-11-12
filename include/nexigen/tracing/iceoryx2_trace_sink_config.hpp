// nexigen::tracing — construction-time configuration for Iceoryx2TraceSink
// (change add-iceoryx2-trace-sink; spec tracing/iceoryx2-trace-sink "Sink
// configuration and diagnostics"; design D7).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>

namespace nexigen::tracing {

/// Iceoryx2TraceSink construction-time configuration. Validated at startup;
/// invalid values are rejected before the publisher or any service is created.
struct Iceoryx2TraceSinkConfig {
    std::string service_name{"nexigen/trace/stream"}; ///< subscription (≤ 511 B)
    std::string app_id{"TRCE"};                       ///< 1–4 byte ASCII code
    std::uint64_t subscriber_max_buffer_depth{1024};  ///< 0 = service default (2)

    /// Startup validation: a non-empty/oversized name or application code is
    /// rejected before any port exists.
    [[nodiscard]] core::Status validate() const noexcept {
        if (service_name.empty() || service_name.size() > 511U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (app_id.empty() || app_id.size() > 4U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        return {};
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        std::string out = "Iceoryx2TraceSinkConfig(service=" + service_name;
        out += ", app=" + app_id;
        out += ", subscriber_max_buffer_depth=" + std::to_string(subscriber_max_buffer_depth) + ")";
        return out;
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const Iceoryx2TraceSinkConfig& config) {
    return config.to_string();
}

/// Cold-path JSON round-trip (mandate 10), hostile-safe under JSON_NOEXCEPTION=1;
/// bodies live in src/tracing/iceoryx2_trace_sink_config.cpp.
void to_json(nlohmann::json& j, const Iceoryx2TraceSinkConfig& config);
void from_json(const nlohmann::json& j, Iceoryx2TraceSinkConfig& out) noexcept;

} // namespace nexigen::tracing

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::tracing::Iceoryx2TraceSinkConfig>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::Iceoryx2TraceSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
