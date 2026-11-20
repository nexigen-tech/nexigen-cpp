// nexigen::chrono — fixed-point two-state Kalman filter (offset, drift).
//
// The daemon feeds every 10 Hz sample of a system-scale offset (`TAI - hub`,
// `REALTIME - hub`, `MONOTONIC - hub`) into one filter. The state is
// `(offset, drift)` with a 2x2 covariance; all arithmetic is integer fixed point
// in Q12 (4096 sub-nanosecond units), so the loop allocates nothing, performs no
// soft-float operation, and is bit-for-bit reproducible.
//
// Units: offset is nanoseconds, drift is nanoseconds per second, covariance is
// ns^2 in Q12.
#pragma once

#include <cstdint>

namespace nexigen::chrono::detail {

/// @brief Tuning of one two-state filter.
struct Kalman2Config {
    /// Measurement variance in ns^2 (for example 1000 ns sigma => 1'000'000).
    std::int64_t measurement_ns2{1'000'000};
    /// Offset process variance added per update, in ns^2.
    std::int64_t process_offset_ns2{1'000};
    /// Drift process variance added per update, in (ns/s)^2 Q12.
    std::int64_t process_drift_q12{1LL << 12U};
};

/// @brief Two-state (offset, drift) fixed-point Kalman filter.
///
/// @note Spec `chrono/time-base`, requirement "Canonical monotonic scale";
///       design D-D.
class Kalman2 {
public:
    /// @brief Construct a filter with the default tuning.
    Kalman2() noexcept = default;

    /// @brief Construct a filter with explicit tuning.
    /// @param config Process and measurement noise.
    explicit Kalman2(Kalman2Config config) noexcept : config_(config) {}

    /// @brief Reset the filter to a known offset and a fresh covariance.
    ///
    /// @param offset_ns Measured offset establishing the new baseline.
    void reset(std::int64_t offset_ns) noexcept;

    /// @brief Whether the filter has a baseline.
    /// @return `true` after `reset()` or the first `update()`.
    /// @note Design D-D (fixed-point 2-state Kalman in the publish loop).
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

    /// @brief Feed one offset measurement.
    ///
    /// @param measured_offset_ns Measured offset in nanoseconds.
    /// @param dt_ns Time since the previous update in nanoseconds; zero reuses
    ///              the stored interval.
    /// @note Allocation-free, deterministic.
    void update(std::int64_t measured_offset_ns, std::uint64_t dt_ns) noexcept;

    /// @brief Current offset estimate in nanoseconds.
    /// @return Offset state in nanoseconds.
    /// @note Design D-D.
    [[nodiscard]] std::int64_t offset_ns() const noexcept { return offset_ns_; }

    /// @brief Current drift estimate in parts per billion.
    /// @return Drift state in ppb (1 ns/s is 1e9 ppb).
    /// @note Design D-D.
    [[nodiscard]] std::int64_t drift_ppb() const noexcept;

    /// @brief Predict the offset at a future instant.
    ///
    /// @param horizon_ns Nanoseconds from the latest update.
    /// @return Predicted offset; the last estimate when uninitialized.
    [[nodiscard]] std::int64_t predict_offset_ns(std::uint64_t horizon_ns) const noexcept;

    /// @brief Current offset variance in ns^2.
    /// @return Offset covariance diagonal in ns^2.
    /// @note Design D-D.
    [[nodiscard]] std::int64_t offset_variance_ns2() const noexcept { return p00_ >> 12U; }

    /// @brief The active tuning.
    /// @return Borrowed view of the process/measurement noise bound at
    ///         construction.
    /// @note Design D-D.
    [[nodiscard]] const Kalman2Config& config() const noexcept { return config_; }

private:
    Kalman2Config config_{};
    std::int64_t offset_ns_{0};   ///< Offset state in nanoseconds.
    std::int64_t drift_q20_{0};   ///< Drift state in ns/s scaled by 2^20.
    std::int64_t p00_{0};         ///< Offset variance, Q12 ns^2.
    std::int64_t p01_{0};         ///< Offset/drift covariance, Q12.
    std::int64_t p11_{0};         ///< Drift variance, Q12 (ns/s)^2.
    std::uint64_t last_dt_ns_{0}; ///< Last accepted interval.
    bool initialized_{false};
};

} // namespace nexigen::chrono::detail
