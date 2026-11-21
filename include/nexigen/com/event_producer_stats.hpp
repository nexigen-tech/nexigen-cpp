// nexigen::com — event producer statistics.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/com/error.hpp>

namespace nexigen::com {

/// @brief Counters maintained by an event producer.
///
/// @note Spec `com/event-system`, requirement "Event production and publish
///       outcome".
struct EventProducerStats {
    std::uint64_t published{0};         ///< Successfully sent samples.
    std::uint64_t send_failures{0};     ///< Failed send attempts.
    std::uint64_t notify_failures{0};   ///< Failed wake notifications.
    Error last_notify_error{Error::Ok}; ///< Last notify failure, if any.
    std::uint64_t current_sequence{0};  ///< Next transport sequence.
    std::uint64_t current_epoch{0};     ///< Active producer epoch.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable statistics.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `EventProducerStats::to_string`.
/// @param stats Statistics to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const EventProducerStats& stats) {
    return stats.to_string();
}

/// @brief Serialize producer statistics to JSON.
/// @param j Destination JSON object.
/// @param stats Statistics to serialize.
void to_json(nlohmann::json& j, const EventProducerStats& stats);

/// @brief Parse producer statistics from JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param stats Destination statistics.
void from_json(const nlohmann::json& j, EventProducerStats& stats) noexcept;

} // namespace nexigen::com

/// @brief Formats producer statistics as their diagnostic string.
template <>
struct std::formatter<nexigen::com::EventProducerStats> : std::formatter<std::string_view> {
    /// @brief Write the statistics diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param stats Statistics to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::EventProducerStats& stats, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(stats.to_string(), ctx);
    }
};
