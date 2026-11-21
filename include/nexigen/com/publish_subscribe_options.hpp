// nexigen::com — optional publish-subscribe service settings.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::com {

/// @brief Optional service-level settings for a publish-subscribe endpoint.
///
/// A zero value means "leave the transport default" for every numeric field.
/// The publisher and subscriber must use compatible settings because iceoryx2
/// verifies service-level options when an existing service is opened.
///
/// @note Spec `com/publish-subscribe`, requirement "Publish-subscribe service
///       options"; the defaults preserve the pre-change behaviour.
struct PublishSubscribeOptions {
    std::uint64_t max_subscribers{0};            ///< Maximum connected subscribers; 0 = default.
    std::uint64_t max_publishers{0};             ///< Maximum connected publishers; 0 = default.
    std::uint64_t max_nodes{0};                  ///< Maximum nodes; 0 = transport default.
    std::uint64_t subscriber_max_buffer_size{0}; ///< Service subscriber depth; 0 = default.
    std::uint64_t subscriber_buffer_size{0};     ///< Per-subscriber depth; 0 = service default.
    std::uint64_t history_size{0};               ///< Samples kept for late subscribers.
    std::uint64_t subscriber_history_request{
        0};                           ///< History requested by a subscriber; 0 = service default.
    bool enable_safe_overflow{false}; ///< Overwrite oldest samples when full.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable option summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `PublishSubscribeOptions::to_string`.
/// @param options Options to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const PublishSubscribeOptions& options) {
    return options.to_string();
}

/// @brief Serialize publish-subscribe options to JSON.
/// @param j Destination JSON object.
/// @param options Options to serialize.
void to_json(nlohmann::json& j, const PublishSubscribeOptions& options);

/// @brief Parse publish-subscribe options from JSON, strictly non-throwing.
/// @param j Source JSON object; malformed values leave `options` unchanged.
/// @param options Destination options.
void from_json(const nlohmann::json& j, PublishSubscribeOptions& options) noexcept;

} // namespace nexigen::com

/// @brief Formats publish-subscribe options as their diagnostic string.
template <>
struct std::formatter<nexigen::com::PublishSubscribeOptions> : std::formatter<std::string_view> {
    /// @brief Write the option diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param options Options to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::PublishSubscribeOptions& options, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(options.to_string(), ctx);
    }
};
