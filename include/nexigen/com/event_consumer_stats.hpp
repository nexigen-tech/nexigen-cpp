// nexigen::com — event consumer statistics.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::com {

/// @brief Counters maintained by an event consumer.
///
/// @note Spec `com/event-system`, requirement "Transport loss detection and
///       callbacks".
struct EventConsumerStats {
    std::uint64_t received{0};          ///< Delivered samples.
    std::uint64_t transport_lost{0};    ///< Exact samples lost to transport gaps.
    std::uint64_t restarts{0};          ///< Producer epoch changes.
    std::uint64_t duplicates{0};        ///< Duplicate/backward samples dropped.
    std::uint64_t unexpected_resets{0}; ///< Sequence-zero resets without epoch change.
    std::uint64_t first_sequence{0};    ///< First observed transport sequence.
    std::uint64_t last_sequence{0};     ///< Last delivered transport sequence.
    std::uint64_t history_skipped{0};   ///< Events before the first sample.
    std::uint64_t current_epoch{0};     ///< Active producer epoch.
    bool has_received{false};           ///< Whether a first sample was observed.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable statistics.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `EventConsumerStats::to_string`.
/// @param stats Statistics to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const EventConsumerStats& stats) {
    return stats.to_string();
}

/// @brief Serialize consumer statistics to JSON.
/// @param j Destination JSON object.
/// @param stats Statistics to serialize.
void to_json(nlohmann::json& j, const EventConsumerStats& stats);

/// @brief Parse consumer statistics from JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param stats Destination statistics.
void from_json(const nlohmann::json& j, EventConsumerStats& stats) noexcept;

} // namespace nexigen::com

/// @brief Formats consumer statistics as their diagnostic string.
template <>
struct std::formatter<nexigen::com::EventConsumerStats> : std::formatter<std::string_view> {
    /// @brief Write the statistics diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param stats Statistics to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::EventConsumerStats& stats, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(stats.to_string(), ctx);
    }
};
