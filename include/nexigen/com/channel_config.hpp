// nexigen::com — event channel configuration.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/com/error.hpp>
#include <nexigen/com/event_options.hpp>
#include <nexigen/com/publish_subscribe_options.hpp>
#include <nexigen/com/timestamp_domain.hpp>

namespace nexigen::com {

/// @brief Bounded configuration shared by an event producer and consumer.
///
/// `max_consumers` is the hard v1 limit of 16. `queue_size` and `history_size`
/// map to the publish-subscribe service settings; `default_timestamp_domain`
/// applies to `publish_now()` events.
///
/// @note Spec `com/event-system`, requirement "Channel configuration and
///       limits".
struct ChannelConfig {
    static constexpr std::uint64_t max_supported_consumers = 16U; ///< v1 consumer hard cap.

    std::uint64_t max_consumers{16}; ///< Maximum consumers, 1..16.
    std::uint64_t queue_size{512};   ///< Per-consumer queue depth.
    std::uint64_t history_size{16};  ///< Late-join history depth.
    TimestampDomain default_timestamp_domain{
        TimestampDomain::MonotonicRaw}; ///< `publish_now` domain.

    /// @brief Validate the consumer, queue, and history bounds.
    /// @return Empty status, or `InvalidArgument` for an invalid value.
    [[nodiscard]] Status validate() const noexcept;

    /// @brief Build the publish-subscribe service options for this channel.
    /// @return Options with safe overflow, history, and consumer limits.
    [[nodiscard]] PublishSubscribeOptions publish_subscribe_options() const noexcept;

    /// @brief Build the event-service options for this channel.
    /// @return Options with listener and notifier limits.
    [[nodiscard]] EventOptions event_options() const noexcept;

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable configuration summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `ChannelConfig::to_string`.
/// @param config Configuration to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const ChannelConfig& config) {
    return config.to_string();
}

/// @brief Serialize a channel configuration to JSON.
/// @param j Destination JSON object.
/// @param config Configuration to serialize.
void to_json(nlohmann::json& j, const ChannelConfig& config);

/// @brief Parse a channel configuration from JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param config Destination configuration.
void from_json(const nlohmann::json& j, ChannelConfig& config) noexcept;

} // namespace nexigen::com

/// @brief Formats a channel configuration as its diagnostic string.
template <> struct std::formatter<nexigen::com::ChannelConfig> : std::formatter<std::string_view> {
    /// @brief Write the configuration diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param config Configuration to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::com::ChannelConfig& config, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(config.to_string(), ctx);
    }
};
