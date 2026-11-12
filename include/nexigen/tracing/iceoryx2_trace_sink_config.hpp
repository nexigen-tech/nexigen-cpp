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

/// @brief Construction-time configuration of `Iceoryx2TraceSink`.
///
/// A plain value type validated once at startup: the service name, the
/// publisher's 1–4 byte application code, and the subscriber buffer depth. An
/// invalid value is rejected before the publisher or any service port is
/// created, so a bad configuration never leaves half-open transport state.
///
/// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Sink configuration
///       and diagnostics".
struct Iceoryx2TraceSinkConfig {
    std::string service_name{"nexigen/trace/stream"}; ///< subscription (≤ 511 B)
    std::string app_id{"TRCE"};                       ///< 1–4 byte ASCII code
    std::uint64_t subscriber_max_buffer_depth{1024};  ///< 0 = service default (2)

    /// @brief Validate the configuration before any transport port exists.
    ///
    /// Rejects an empty or oversized service name (`> 511` bytes) and an empty
    /// or oversized application code (`> 4` bytes).
    ///
    /// @return Success when all fields are in range, otherwise
    ///         `core::ErrorCode::InvalidArgument`.
    /// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Sink configuration
    ///       and diagnostics".
    [[nodiscard]] core::Status validate() const noexcept {
        if (service_name.empty() || service_name.size() > 511U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (app_id.empty() || app_id.size() > 4U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        return {};
    }

    /// @brief Render the configuration as a cold-path diagnostic string.
    ///
    /// @return Text naming the service, application code, and subscriber depth.
    /// @note Project mandate 9 (`to_string`); diagnostic only.
    [[nodiscard]] std::string to_string() const {
        std::string out = "Iceoryx2TraceSinkConfig(service=" + service_name;
        out += ", app=" + app_id;
        out += ", subscriber_max_buffer_depth=" + std::to_string(subscriber_max_buffer_depth) + ")";
        return out;
    }
};

/// @brief Free diagnostic helper mirroring
///        `Iceoryx2TraceSinkConfig::to_string()`.
///
/// @param config Configuration to render.
/// @return The same text the member returns.
/// @note Project mandate 9 (`to_string`); cold path only.
[[nodiscard]] inline std::string to_string(const Iceoryx2TraceSinkConfig& config) {
    return config.to_string();
}

/// @brief Serialize the configuration as JSON (cold path).
///
/// @param j Destination JSON value, overwritten with the configuration fields.
/// @param config Configuration to serialize.
/// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Sink configuration
///       and diagnostics"; project mandate 10 (JSON).
void to_json(nlohmann::json& j, const Iceoryx2TraceSinkConfig& config);

/// @brief Parse the configuration from JSON without throwing.
///
/// Safe under `JSON_NOEXCEPTION=1`: missing or wrongly typed fields keep their
/// prior value and a hostile document never aborts.
///
/// @param j JSON object to read.
/// @param out Destination configuration, updated field by field.
/// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Sink configuration
///       and diagnostics"; project mandate 10 (JSON).
void from_json(const nlohmann::json& j, Iceoryx2TraceSinkConfig& out) noexcept;

} // namespace nexigen::tracing

/// @brief `std::formatter` for `Iceoryx2TraceSinkConfig`.
///
/// @note Project mandate 11 (`std::formatter`); formatting is cold path only.
template <>
struct std::formatter<nexigen::tracing::Iceoryx2TraceSinkConfig>
    : std::formatter<std::string_view> {
    /// @brief Format the configuration through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param config Configuration to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::Iceoryx2TraceSinkConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
