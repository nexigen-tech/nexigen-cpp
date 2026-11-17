// nexigen::com — optional event-service settings.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::com {

/// @brief Optional service-level settings for an event notifier or listener.
///
/// A zero value means "leave the transport default" for every numeric field.
/// Notifiers and listeners must agree on the service-level settings.
///
/// @note Spec `com/publish-subscribe`, requirement "Event service options".
struct EventOptions {
    std::uint64_t max_listeners{0}; ///< Maximum listeners; 0 = transport default.
    std::uint64_t max_notifiers{0}; ///< Maximum notifiers; 0 = transport default.
    std::uint64_t max_nodes{0};     ///< Maximum nodes; 0 = transport default.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable option summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `EventOptions::to_string`.
/// @param options Options to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const EventOptions& options) {
    return options.to_string();
}

/// @brief Serialize event options to JSON.
/// @param j Destination JSON object.
/// @param options Options to serialize.
void to_json(nlohmann::json& j, const EventOptions& options);

/// @brief Parse event options from JSON, strictly non-throwing.
/// @param j Source JSON object; malformed values leave `options` unchanged.
/// @param options Destination options.
void from_json(const nlohmann::json& j, EventOptions& options) noexcept;

} // namespace nexigen::com

/// @brief Formats event options as their diagnostic string.
template <> struct std::formatter<nexigen::com::EventOptions> : std::formatter<std::string_view> {
    /// @brief Write the option diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param options Options to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::com::EventOptions& options, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(options.to_string(), ctx);
    }
};
