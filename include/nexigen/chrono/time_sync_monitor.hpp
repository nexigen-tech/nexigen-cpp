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
/// @note Spec `chrono/time-sync`, requirements "Time synchronization
///       classification" and "Threshold policy".
class TimeSyncMonitor {
public:
    /// @brief Bind the monitor to a threshold policy.
    explicit TimeSyncMonitor(TimeSyncPolicy policy = {}) noexcept : policy_(policy) {}

    /// @brief Classify one offset transition (pure, testable).
    ///
    /// @param previous_offset_ns Previous `REALTIME - MONOTONIC_RAW`.
    /// @param current_offset_ns Current offset.
    /// @param elapsed_ns Monotonic time between the samples.
    /// @param policy Thresholds.
    /// @return `GoingBackwards`, `Stepped`, `RateExceeded`, or `Ok`.
    [[nodiscard]] static TimeSyncStatus classify(std::int64_t previous_offset_ns,
                                                 std::int64_t current_offset_ns,
                                                 std::int64_t elapsed_ns,
                                                 const TimeSyncPolicy& policy) noexcept;

    /// @brief Read the clocks and classify against the previous sample.
    ///
    /// @return The sample, or `std::nullopt` when a clock read fails.
    [[nodiscard]] std::optional<TimeSyncSample> sample() noexcept;

    /// @brief The active policy.
    [[nodiscard]] const TimeSyncPolicy& policy() const noexcept { return policy_; }

    /// @brief Drop the baseline; the next sample starts over.
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
