// nexigen-canview — why a synthetic loss record exists.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canview {

/// @brief Cause of one synthetic loss record.
///
/// Eight values fit the three `GapReason` bits of `record_meta`.
enum class GapReason : std::uint8_t {
    HardwareSequence,          ///< Per-channel hardware sequence skipped values.
    TransportGap,              ///< `com::LossInfo` reported a transport loss.
    TransportRestart,          ///< The transport restarted (publisher restart).
    TransportDuplicate,        ///< The transport reported duplicates.
    IngressOverflow,           ///< The capture-to-UI queue overflowed.
    DriverDropped,             ///< `CanChannelStatus::dropped_count` grew.
    DriverReportedSequenceGap, ///< Status reported a sequence gap without frame sequences.
    SequenceReset,             ///< A sequence jump was too large; history resets.
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `GapReason`.
template <> struct EnumTraits<nexigen::canview::GapReason> {
    /// @brief One value/name row.
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::GapReason value; ///< Enumerator. ///< Enumerator.
        std::string_view name;             ///< Canonical name.
    };
    /// @brief Canonical table.
    /// @brief Canonical table.
    static constexpr std::array<Entry, 8> entries{{
        {nexigen::canview::GapReason::HardwareSequence, "HardwareSequence"},
        {nexigen::canview::GapReason::TransportGap, "TransportGap"},
        {nexigen::canview::GapReason::TransportRestart, "TransportRestart"},
        {nexigen::canview::GapReason::TransportDuplicate, "TransportDuplicate"},
        {nexigen::canview::GapReason::IngressOverflow, "IngressOverflow"},
        {nexigen::canview::GapReason::DriverDropped, "DriverDropped"},
        {nexigen::canview::GapReason::DriverReportedSequenceGap, "DriverReportedSequenceGap"},
        {nexigen::canview::GapReason::SequenceReset, "SequenceReset"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical name of a gap reason.
/// @param reason Reason to render.
/// @return Canonical name or `Unknown`.
[[nodiscard]] constexpr std::string_view to_string(GapReason reason) noexcept {
    return core::enum_name(reason);
}

/// @brief Parse a canonical gap reason.
/// @param name Candidate name.
/// @param out Destination, unchanged on a miss.
/// @return `true` when a name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, GapReason& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write a gap reason as JSON.
/// @param j Destination.
/// @param reason Reason to serialize.
inline void to_json(nlohmann::json& j, const GapReason reason) {
    core::mandate::enum_to_json(j, reason);
}

/// @brief Read a gap reason from JSON without throwing.
/// @param j Source.
/// @param reason Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, GapReason& reason) noexcept {
    core::mandate::enum_from_json(j, reason);
}

} // namespace nexigen::canview
