// nexigen::tracing — DroppedStats (change refactor-hub-model-config-headers;
// extracted verbatim from tracer_provider.hpp).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::tracing {

/// @brief Snapshot of dropped-span accounting, one counter per drop cause.
///
/// The three drop causes are counted separately and read from any thread at any
/// time as a relaxed aggregate observation, never as synchronization. Spans
/// filtered out by sampling are not counted here because a sampling decision is
/// not a failure. The snapshot is a plain value type: it carries no thread and
/// no lifetime, so callers may copy and store it freely.
///
/// @note Spec `tracing/tracer-provider`, requirement "Drop visibility".
struct DroppedStats {
    std::uint64_t rate_limited{};   ///< Rejected by the span-rate budget.
    std::uint64_t buffer_full{};    ///< Rejected by a full shard ring.
    std::uint64_t slot_exhausted{}; ///< Dropped: no free shard slot.

    /// @brief Sum the three drop causes.
    ///
    /// @return Total number of dropped spans across all causes.
    /// @note Spec `tracing/tracer-provider`, requirement "Drop visibility".
    [[nodiscard]] constexpr std::uint64_t total() const noexcept {
        return rate_limited + buffer_full + slot_exhausted;
    }

    /// @brief Render the snapshot as a cold-path diagnostic string.
    ///
    /// @return Text naming each drop cause and the total.
    /// @note Project mandate 9 (`to_string`); diagnostic only.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `DroppedStats::to_string()`.
///
/// @param stats Snapshot to render.
/// @return The same text the member returns.
/// @note Project mandate 9 (`to_string`); cold path only.
[[nodiscard]] std::string to_string(const DroppedStats& stats);

/// @brief Serialize the snapshot as JSON (cold path).
///
/// @param j Destination JSON value, overwritten with the three counters.
/// @param stats Snapshot to serialize.
/// @note Spec `tracing/record-format`, requirement "DroppedStats
///       deserialization is non-throwing and partial-field safe"; project
///       mandate 10 (JSON).
void to_json(nlohmann::json& j, const DroppedStats& stats);

/// @brief Parse the snapshot from JSON without throwing.
///
/// Because the snapshot is a value type it round-trips completely; fields that
/// are missing or wrongly typed keep their prior value, and a hostile document
/// never aborts under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON object to read.
/// @param out Destination snapshot, updated field by field.
/// @note Spec `tracing/record-format`, requirement "DroppedStats
///       deserialization is non-throwing and partial-field safe"; project
///       mandate 10 (JSON).
void from_json(const nlohmann::json& j, DroppedStats& out) noexcept;

} // namespace nexigen::tracing

/// @brief `std::formatter` for `DroppedStats`.
///
/// @note Project mandate 11 (`std::formatter`); formatting is cold path only.
template <>
struct std::formatter<nexigen::tracing::DroppedStats> : std::formatter<std::string_view> {
    /// @brief Format the snapshot through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param stats Snapshot to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::tracing::DroppedStats& stats, Ctx& ctx) const {
        const std::string text = stats.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
