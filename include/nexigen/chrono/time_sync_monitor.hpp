// nexigen::chrono — realtime/monotonic offset monitor (change
// time-sync-supervision).
#pragma once

#include <cstdint>
#include <optional>

#include <nexigen/chrono/time_sync_policy.hpp>
#include <nexigen/chrono/time_sync_sample.hpp>
#include <nexigen/chrono/time_sync_status.hpp>

namespace nexigen::chrono {

/// @brief Detects realtime clock steps, backwards jumps, and excessive slew.
///
/// The caller samples on its own schedule; `sample()` reads `CLOCK_REALTIME`
/// and `CLOCK_MONOTONIC_RAW`, computes their offset, and classifies the change
/// against the policy. The first sample only establishes the baseline.
///
/// The monitor is a passive detector: it allocates nothing, starts no thread,
/// and never reacts on its own. The reaction (log, degrade, watchdog reset) is
/// the application's or PHM's decision, taken from the returned status.
///
/// @note Spec `chrono/time-sync`, requirements "Time synchronization
///       classification", "Threshold policy", and "Monitor diagnostics".
class TimeSyncMonitor {
public:
    /// @brief Bind the monitor to a threshold policy.
    ///
    /// @param policy Thresholds used by later `sample()` calls; copied into the
    ///        monitor. The default policy is the conservative production set.
    /// @note Spec `chrono/time-sync`, requirement "Threshold policy".
    explicit TimeSyncMonitor(TimeSyncPolicy policy = {}) noexcept : policy_(policy) {}

    /// @brief Classify one offset transition (pure, testable).
    ///
    /// Compares a previous and a current offset without touching a clock. A
    /// backward jump beyond `max_step_ns` wins over a forward step; a step
    /// outside the tolerance wins over the rate check. The rate is evaluated
    /// only when the elapsed time reaches `min_window_ns` and the step stays
    /// inside `max_step_ns`, so the ppm quotient is meaningful and cannot
    /// overflow.
    ///
    /// @param previous_offset_ns Previous `REALTIME - MONOTONIC_RAW`.
    /// @param current_offset_ns Current offset.
    /// @param elapsed_ns Monotonic time between the samples.
    /// @param policy Thresholds.
    /// @return `GoingBackwards`, `Stepped`, `RateExceeded`, or `Ok`.
    /// @note Spec `chrono/time-sync`, requirement "Time synchronization
    ///       classification" and scenario "The minimum window suppresses rate
    ///       noise".
    [[nodiscard]] static TimeSyncStatus classify(std::int64_t previous_offset_ns,
                                                 std::int64_t current_offset_ns,
                                                 std::int64_t elapsed_ns,
                                                 const TimeSyncPolicy& policy) noexcept;

    /// @brief Read the clocks and classify against the previous sample.
    ///
    /// Reads `CLOCK_REALTIME` and `CLOCK_MONOTONIC_RAW`, stores the new offset
    /// as the baseline for the next call, and classifies the transition against
    /// the constructor policy. A failed clock read leaves the baseline
    /// untouched, so the next successful call compares against the last valid
    /// sample.
    ///
    /// @return The sample, or `std::nullopt` when a clock read fails.
    /// @note Spec `chrono/time-sync`, requirement "Time synchronization
    ///       classification"; the call is hot-path safe (no allocation and no
    ///       blocking).
    [[nodiscard]] std::optional<TimeSyncSample> sample() noexcept;

    /// @brief The active policy.
    ///
    /// @return Borrowed view of the thresholds bound at construction; valid for
    ///         the monitor's lifetime.
    [[nodiscard]] const TimeSyncPolicy& policy() const noexcept { return policy_; }

    /// @brief Drop the baseline; the next sample starts over.
    ///
    /// The next `sample()` behaves like the first one: it stores the offset and
    /// reports `Ok` with zero step and rate. Useful after a deliberate clock
    /// change that the application already handled.
    void reset() noexcept {
        previous_offset_ns_.reset();
        previous_mono_ns_.reset();
    }

private:
    TimeSyncPolicy policy_;                          ///< Thresholds.
    std::optional<std::int64_t> previous_offset_ns_; ///< Last offset.
    std::optional<std::int64_t> previous_mono_ns_;   ///< Last monotonic reading.
};

} // namespace nexigen::chrono
