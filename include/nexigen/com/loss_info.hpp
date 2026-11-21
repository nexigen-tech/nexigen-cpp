// nexigen::com — event loss diagnostics.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/com/loss_reason.hpp>
#include <nexigen/com/timestamp_domain.hpp>

namespace nexigen::com {

/// @brief One loss, duplicate, or restart observation.
///
/// `lost_count` is exact only for `TransportGap`; restart, duplicate, and
/// unexpected reset report zero because the exact missing range is unknown.
///
/// @note Spec `com/event-system`, requirement "Transport loss detection and
///       callbacks".
struct LossInfo {
    LossReason reason{LossReason::TransportGap}; ///< Classification.
    std::uint64_t expected_sequence{0};          ///< Expected transport sequence.
    std::uint64_t actual_sequence{0};            ///< Observed transport sequence.
    std::uint64_t lost_count{0};                 ///< Exact only for `TransportGap`.
    std::uint64_t old_epoch{0};                  ///< Previous producer epoch.
    std::uint64_t new_epoch{0};                  ///< Current producer epoch.
    std::uint64_t actual_timestamp_ns{0};        ///< Timestamp of the triggering event.
    TimestampDomain actual_timestamp_domain{
        TimestampDomain::Unknown}; ///< Domain of that timestamp.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable loss summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `LossInfo::to_string`.
/// @param info Info to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const LossInfo& info) {
    return info.to_string();
}

/// @brief Serialize loss info to JSON.
/// @param j Destination JSON object.
/// @param info Info to serialize.
void to_json(nlohmann::json& j, const LossInfo& info);

/// @brief Parse loss info from JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param info Destination info.
void from_json(const nlohmann::json& j, LossInfo& info) noexcept;

} // namespace nexigen::com

/// @brief Formats loss info as its diagnostic string.
template <> struct std::formatter<nexigen::com::LossInfo> : std::formatter<std::string_view> {
    /// @brief Write the loss diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param info Info to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::com::LossInfo& info, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(info.to_string(), ctx);
    }
};
