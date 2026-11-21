// nexigen::com — explicit event time.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/com/timestamp_domain.hpp>

namespace nexigen::com {

/// @brief One event timestamp and the domain it belongs to.
///
/// @note Spec `com/event-system`, requirement "Event header and timestamp
///       provenance".
struct EventTime {
    std::uint64_t ns{0};                              ///< Nanoseconds in `domain`.
    TimestampDomain domain{TimestampDomain::Unknown}; ///< Timestamp domain.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable time summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `EventTime::to_string`.
/// @param time Time to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const EventTime& time) {
    return time.to_string();
}

/// @brief Serialize an event time to JSON.
/// @param j Destination JSON object.
/// @param time Time to serialize.
void to_json(nlohmann::json& j, const EventTime& time);

/// @brief Parse an event time from JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param time Destination time.
void from_json(const nlohmann::json& j, EventTime& time) noexcept;

} // namespace nexigen::com

/// @brief Formats an event time as its diagnostic string.
template <> struct std::formatter<nexigen::com::EventTime> : std::formatter<std::string_view> {
    /// @brief Write the time diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param time Time to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::com::EventTime& time, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(time.to_string(), ctx);
    }
};
