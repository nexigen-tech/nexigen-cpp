// nexigen::synchro — deterministic phase schedule
// (change add-synchro-event-sync; spec synchro/protocol).
//
// Every deadline is derived from one absolute start instant and the absolute
// phase index:
//
//   T(c, p) = T_start + c * Tc + floor(p * Tc / N)
//
// Nothing is derived from the previous wake-up, so a missed phase never moves
// the grid. The arithmetic is constexpr, allocation-free, and lock-free.
#pragma once

#include <cstdint>

#include <nexigen/synchro/start_policy.hpp>
#include <nexigen/synchro/sync_error.hpp>

namespace nexigen::synchro {

/// @brief Result of resolving a start policy against "now".
///
/// `realigned` is set when an `AtTarget` policy in the past was silently moved
/// to the next cycle; the caller logs the warning.
///
/// @note Spec `synchro/protocol`, requirement "Start policy".
struct StartResolution {
    std::uint64_t start_time_ns{0}; ///< Resolved first-cycle instant.
    bool realigned{false};          ///< Past target was moved to the next cycle.
};

/// @brief Result of classifying an overrun against the absolute grid.
///
/// `next_phase` is the first slot whose deadline is still in the future;
/// `skipped` is the exact number of stale slots before it (zero when the
/// current slot may still be published late).
///
/// @note Spec `synchro/generator`, requirement "Overrun handling".
struct OverrunSkip {
    std::uint64_t next_phase{0}; ///< First future absolute phase slot.
    std::uint64_t skipped{0};    ///< Stale slots skipped before `next_phase`.
};

/// @brief Resolve the first-cycle instant of a generator.
///
/// `NextCycle` ignores the target and returns the first cycle boundary strictly
/// after `now_ns`. `AtTarget` uses the override when `has_target_override` is
/// set, otherwise the policy target; a future target is either used exactly or
/// snapped to the next boundary, and a past target is rejected or realigned
/// according to `PastTargetPolicy`.
///
/// @param now_ns Current raw-monotonic time.
/// @param cycle_period_ns Cycle period Tc; must be positive.
/// @param policy Configured start policy.
/// @param has_target_override `true` when `start_at()` supplied a one-shot target.
/// @param target_override_ns One-shot target used when the flag is set.
/// @return The resolved start, or `InvalidConfig` when a past target is rejected.
/// @note Spec `synchro/protocol`, requirement "Start policy".
[[nodiscard]] SyncResult<StartResolution>
resolve_start(std::uint64_t now_ns, std::uint64_t cycle_period_ns, const StartPolicy& policy,
              bool has_target_override = false, std::uint64_t target_override_ns = 0) noexcept;

/// @brief Absolute phase grid of one generator lifecycle.
///
/// @note Spec `synchro/protocol`, requirement "Deterministic phase schedule".
class SyncSchedule {
public:
    /// @brief Bind the grid to a resolved start instant.
    /// @param start_time_ns First phase deadline of cycle zero.
    /// @param cycle_period_ns Cycle period Tc.
    /// @param phases_per_cycle Phases per cycle N.
    /// @note Spec `synchro/protocol`, requirement "Deterministic phase
    ///       schedule".
    constexpr SyncSchedule(std::uint64_t start_time_ns, std::uint64_t cycle_period_ns,
                           std::uint32_t phases_per_cycle) noexcept
        : start_time_ns_(start_time_ns), cycle_period_ns_(cycle_period_ns),
          phases_per_cycle_(phases_per_cycle) {}

    /// @brief First phase deadline of cycle zero.
    /// @return Raw-monotonic start instant.
    /// @note Spec `synchro/protocol`, requirement "Deterministic phase schedule".
    [[nodiscard]] constexpr std::uint64_t start_time_ns() const noexcept { return start_time_ns_; }
    /// @brief Cycle period Tc.
    /// @return Nanoseconds per cycle.
    /// @note Spec `synchro/protocol`, requirement "Deterministic phase schedule".
    [[nodiscard]] constexpr std::uint64_t cycle_period_ns() const noexcept {
        return cycle_period_ns_;
    }
    /// @brief Phases per cycle N.
    /// @return Phase count.
    /// @note Spec `synchro/protocol`, requirement "Deterministic phase schedule".
    [[nodiscard]] constexpr std::uint32_t phases_per_cycle() const noexcept {
        return phases_per_cycle_;
    }
    /// @brief Nominal phase duration `Tc / N`, truncated.
    /// @return Nanoseconds per phase, or zero for an empty cycle.
    /// @note Spec `synchro/protocol`, requirement "Deterministic phase schedule".
    [[nodiscard]] constexpr std::uint64_t phase_period_ns() const noexcept {
        return phases_per_cycle_ == 0U ? 0U : cycle_period_ns_ / phases_per_cycle_;
    }

    /// @brief Deadline of one absolute phase slot.
    /// @param absolute_phase Slot index since the generator started.
    /// @return Raw-monotonic deadline of that slot.
    /// @note Spec `synchro/protocol`, requirement "Deterministic phase schedule".
    [[nodiscard]] constexpr std::uint64_t deadline(std::uint64_t absolute_phase) const noexcept {
        if (phases_per_cycle_ == 0U) {
            return start_time_ns_;
        }
        const std::uint64_t cycle = absolute_phase / phases_per_cycle_;
        const std::uint64_t phase = absolute_phase % phases_per_cycle_;
        return start_time_ns_ + cycle * cycle_period_ns_ +
               phase * cycle_period_ns_ / phases_per_cycle_;
    }

    /// @brief Start instant of one cycle.
    /// @param cycle_number Zero-based cycle index.
    /// @return Raw-monotonic cycle start.
    /// @note Spec `synchro/protocol`, requirement "Deterministic phase schedule".
    [[nodiscard]] constexpr std::uint64_t cycle_start(std::uint64_t cycle_number) const noexcept {
        return start_time_ns_ + cycle_number * cycle_period_ns_;
    }

    /// @brief First cycle boundary at or after `time_ns`.
    /// @param time_ns Instant to align.
    /// @return Boundary inside the grid.
    /// @note Spec `synchro/protocol`, requirement "Start policy".
    [[nodiscard]] constexpr std::uint64_t align_up(std::uint64_t time_ns) const noexcept {
        if (time_ns <= start_time_ns_ || cycle_period_ns_ == 0U) {
            return start_time_ns_;
        }
        const std::uint64_t delta = time_ns - start_time_ns_;
        const std::uint64_t cycles = (delta + cycle_period_ns_ - 1U) / cycle_period_ns_;
        return start_time_ns_ + cycles * cycle_period_ns_;
    }

    /// @brief First cycle boundary strictly after `time_ns`.
    /// @param time_ns Instant to pass.
    /// @return Boundary strictly greater than `time_ns`.
    /// @note Spec `synchro/protocol`, requirement "Start policy".
    [[nodiscard]] constexpr std::uint64_t next_boundary(std::uint64_t time_ns) const noexcept {
        if (time_ns < start_time_ns_ || cycle_period_ns_ == 0U) {
            return start_time_ns_;
        }
        const std::uint64_t delta = time_ns - start_time_ns_;
        const std::uint64_t cycles = delta / cycle_period_ns_ + 1U;
        return start_time_ns_ + cycles * cycle_period_ns_;
    }

    /// @brief Absolute phase slot whose deadline is at or before `time_ns`.
    ///
    /// Used for diagnostics and for mapping a wall instant onto the grid
    /// without any state: the result is a pure function of `T_start`, `Tc`,
    /// and `N`.
    ///
    /// @param time_ns Instant to locate; before `T_start` clamps to slot zero.
    /// @return Absolute slot index at or before `time_ns`.
    /// @note Spec `synchro/protocol`, requirement "Deterministic phase
    ///       schedule".
    [[nodiscard]] constexpr std::uint64_t absolute_phase_at(std::uint64_t time_ns) const noexcept {
        if (phases_per_cycle_ == 0U || cycle_period_ns_ == 0U || time_ns <= start_time_ns_) {
            return 0U;
        }
        const std::uint64_t delta = time_ns - start_time_ns_;
        const std::uint64_t cycle = delta / cycle_period_ns_;
        const std::uint64_t rem = delta % cycle_period_ns_;
        const std::uint64_t phase =
            rem * static_cast<std::uint64_t>(phases_per_cycle_) / cycle_period_ns_;
        return cycle * static_cast<std::uint64_t>(phases_per_cycle_) + phase;
    }

    /// @brief Classify an overrun for one absolute slot.
    ///
    /// When the next slot deadline is still in the future, the current slot is
    /// publishable late and nothing is skipped. Otherwise every stale slot is
    /// counted and the result points at the first slot whose deadline is still
    /// in the future. The count spans cycle boundaries because the absolute
    /// grid does.
    ///
    /// @param absolute_phase Slot the generator woke up for.
    /// @param now_ns Raw time sampled after the wake-up.
    /// @return First publishable slot and the exact number of skipped slots.
    /// @note Spec `synchro/generator`, requirement "Overrun handling"; design
    ///       D6.
    [[nodiscard]] constexpr OverrunSkip classify_overrun(std::uint64_t absolute_phase,
                                                         std::uint64_t now_ns) const noexcept {
        if (phases_per_cycle_ == 0U || cycle_period_ns_ == 0U) {
            return OverrunSkip{absolute_phase, 0U};
        }
        if (deadline(absolute_phase + 1U) > now_ns) {
            return OverrunSkip{absolute_phase, 0U};
        }
        std::uint64_t phase = absolute_phase;
        std::uint64_t skipped = 0U;
        while (deadline(phase + 1U) <= now_ns) {
            ++phase;
            ++skipped;
        }
        // The loop stops on the last stale slot; count it and move one past.
        ++skipped;
        ++phase;
        return OverrunSkip{phase, skipped};
    }

private:
    std::uint64_t start_time_ns_{0};    ///< First phase deadline.
    std::uint64_t cycle_period_ns_{0};  ///< Cycle period Tc.
    std::uint32_t phases_per_cycle_{0}; ///< Phases per cycle N.
};

} // namespace nexigen::synchro
