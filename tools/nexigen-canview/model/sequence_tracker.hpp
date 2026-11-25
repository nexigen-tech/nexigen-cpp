// nexigen-canview — per-channel hardware-sequence bookkeeping.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>
#include <nexigen/hw/can/can_message.hpp>

namespace nexigen::canview {

/// @brief Classification of one hardware-sequence observation.
enum class SequenceOutcome : std::uint8_t {
    Unavailable,   ///< The message carries no hardware sequence.
    First,         ///< First sequenced message; baseline established.
    InOrder,       ///< Exactly the previous sequence plus one.
    Gap,           ///< Forward jump; `lost_before` holds the missing count.
    Duplicate,     ///< Same sequence again.
    Reorder,       ///< Sequence moved backwards.
    Reset,         ///< Baseline restarted after a controller restart/bus-off.
    SequenceReset, ///< Jump beyond the reset threshold; no giant gap recorded.
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `SequenceOutcome`.
template <> struct EnumTraits<nexigen::canview::SequenceOutcome> {
    /// @brief One value/name row.
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::SequenceOutcome value; ///< Enumerator. ///< Enumerator.
        std::string_view name;                   ///< Canonical name.
    };
    /// @brief Canonical table.
    /// @brief Canonical table.
    static constexpr std::array<Entry, 8> entries{{
        {nexigen::canview::SequenceOutcome::Unavailable, "Unavailable"},
        {nexigen::canview::SequenceOutcome::First, "First"},
        {nexigen::canview::SequenceOutcome::InOrder, "InOrder"},
        {nexigen::canview::SequenceOutcome::Gap, "Gap"},
        {nexigen::canview::SequenceOutcome::Duplicate, "Duplicate"},
        {nexigen::canview::SequenceOutcome::Reorder, "Reorder"},
        {nexigen::canview::SequenceOutcome::Reset, "Reset"},
        {nexigen::canview::SequenceOutcome::SequenceReset, "SequenceReset"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical name of a sequence outcome.
/// @param outcome Outcome to render.
/// @return Canonical name or `Unknown`.
[[nodiscard]] constexpr std::string_view to_string(SequenceOutcome outcome) noexcept {
    return core::enum_name(outcome);
}

/// @brief Parse a canonical sequence outcome.
/// @param name Candidate name.
/// @param out Destination, unchanged on a miss.
/// @return `true` when a name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, SequenceOutcome& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write a sequence outcome as JSON.
/// @param j Destination.
/// @param outcome Outcome to serialize.
inline void to_json(nlohmann::json& j, const SequenceOutcome outcome) {
    core::mandate::enum_to_json(j, outcome);
}

/// @brief Read a sequence outcome from JSON without throwing.
/// @param j Source.
/// @param outcome Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, SequenceOutcome& outcome) noexcept {
    core::mandate::enum_from_json(j, outcome);
}

/// @brief Result of observing one message.
struct SequenceObservation {
    SequenceOutcome outcome{SequenceOutcome::Unavailable}; ///< Classification.
    std::uint64_t lost_before{0};                          ///< Missing count for `Gap`, else zero.
};

/// @brief Per-channel hardware-sequence tracker.
///
/// The capture thread owns one instance per live channel. The tracker is
/// allocation-free and never blocks; callers turn a `Gap` observation into a
/// `TransportGap(HardwareSequence)` record and a `SequenceReset` into a
/// `SequenceReset` record.
class SequenceTracker {
public:
    /// @brief Default threshold above which a jump is treated as a reset.
    static constexpr std::uint64_t default_max_gap{1'000'000ULL};

    /// @brief Construct a tracker.
    /// @param max_gap Largest forward jump still classified as `Gap`.
    explicit SequenceTracker(std::uint64_t max_gap = default_max_gap) noexcept
        : max_gap_(max_gap == 0U ? default_max_gap : max_gap) {}

    /// @brief Observe one message.
    /// @param message Message whose hardware sequence is classified.
    /// @return The classification and, for `Gap`, the missing count.
    [[nodiscard]] SequenceObservation observe(const nexigen::hw::can::CanMessage& message) noexcept;

    /// @brief Mark that the controller restarted or reached bus-off.
    ///
    /// The next sequenced message re-establishes the baseline and reports
    /// `Reset` instead of a gap.
    void note_controller_reset() noexcept { reset_pending_ = true; }

    /// @brief Forget the baseline (for example after a capture restart).
    void reset() noexcept {
        seen_ = false;
        last_ = 0;
        reset_pending_ = false;
    }

    /// @brief Whether a baseline exists.
    /// @return `true` after the first sequenced message.
    [[nodiscard]] bool has_baseline() const noexcept { return seen_; }

    /// @brief Last accepted sequence.
    /// @return Sequence value, meaningless before the first message.
    [[nodiscard]] std::uint64_t last_sequence() const noexcept { return last_; }

private:
    std::uint64_t max_gap_{default_max_gap}; ///< Reset threshold.
    std::uint64_t last_{0};                  ///< Last accepted sequence.
    bool seen_{false};                       ///< Whether `last_` is valid.
    bool reset_pending_{false};              ///< Next message starts a new baseline.
};

} // namespace nexigen::canview
