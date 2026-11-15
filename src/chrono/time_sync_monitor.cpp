// nexigen::chrono — realtime/monotonic offset monitor implementation (change
// time-sync-supervision).
#include <nexigen/chrono/time_sync_monitor.hpp>

#include <ctime>
#include <limits>

#include <nexigen/core/detail/fault_injection.hpp>

namespace nexigen::chrono {

namespace {

constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000LL;

/// @brief Parts-per-million scale used by the rate diagnostic.
constexpr std::int64_t kPpmScale = 1'000'000LL;

/// @brief Convert a timespec to nanoseconds.
[[nodiscard]] constexpr std::int64_t to_ns(const timespec& value) noexcept {
    return static_cast<std::int64_t>(value.tv_sec) * kNanosecondsPerSecond +
           static_cast<std::int64_t>(value.tv_nsec);
}

/// @brief Step rate in ppm, evaluated only when the quotient is meaningful.
///
/// The rate is reported only while the window is long enough; outside it the
/// value is 0. Callers also guarantee that the step stays inside the policy
/// tolerance, so the ppm product cannot overflow; the result feeds both the
/// classification and the diagnostic sample.
///
/// @param step_ns Offset change since the previous sample.
/// @param elapsed_ns Monotonic time between the samples.
/// @param policy Thresholds.
/// @return The rate in ppm, or 0 when it is not evaluated.
[[nodiscard]] constexpr std::int64_t evaluate_rate_ppm(std::int64_t step_ns,
                                                       std::int64_t elapsed_ns,
                                                       const TimeSyncPolicy& policy) noexcept {
    if (elapsed_ns <= 0 || elapsed_ns < policy.min_window_ns) {
        return 0;
    }
    const std::int64_t max_step_ns = policy.max_step_ns < 0 ? 0 : policy.max_step_ns;
    if (max_step_ns > std::numeric_limits<std::int64_t>::max() / kPpmScale) {
        // A pathological tolerance: scale down before multiplying so the
        // product cannot overflow. Precision below one ppm is irrelevant here.
        return (step_ns / elapsed_ns) * kPpmScale;
    }
    return (step_ns * kPpmScale) / elapsed_ns;
}

} // namespace

TimeSyncStatus TimeSyncMonitor::classify(std::int64_t previous_offset_ns,
                                         std::int64_t current_offset_ns, std::int64_t elapsed_ns,
                                         const TimeSyncPolicy& policy) noexcept {
    const std::int64_t step_ns = current_offset_ns - previous_offset_ns;
    const std::int64_t max_step_ns = policy.max_step_ns < 0 ? 0 : policy.max_step_ns;
    if (step_ns < -max_step_ns) {
        return TimeSyncStatus::GoingBackwards;
    }
    if (step_ns > max_step_ns) {
        return TimeSyncStatus::Stepped;
    }
    const std::int64_t rate_ppm = evaluate_rate_ppm(step_ns, elapsed_ns, policy);
    if (rate_ppm > policy.max_rate_ppm || rate_ppm < -policy.max_rate_ppm) {
        return TimeSyncStatus::RateExceeded;
    }
    return TimeSyncStatus::Ok;
}

std::optional<TimeSyncSample> TimeSyncMonitor::sample() noexcept {
    timespec realtime{};
    timespec monotonic{};
    if (core::fault_injection::fault(core::fault_injection::FaultPoint::Chrono_clock_gettime) ||
        ::clock_gettime(CLOCK_REALTIME, &realtime) != 0 ||
        ::clock_gettime(CLOCK_MONOTONIC_RAW, &monotonic) != 0) {
        return std::nullopt;
    }
    const std::int64_t realtime_ns = to_ns(realtime);
    const std::int64_t monotonic_ns = to_ns(monotonic);
    const std::int64_t offset_ns = realtime_ns - monotonic_ns;

    TimeSyncSample result{};
    result.offset_ns = offset_ns;
    if (!previous_offset_ns_.has_value() || !previous_mono_ns_.has_value()) {
        result.status = TimeSyncStatus::Ok;
    } else {
        const std::int64_t elapsed_ns = monotonic_ns - *previous_mono_ns_;
        const std::int64_t max_step_ns = policy_.max_step_ns < 0 ? 0 : policy_.max_step_ns;
        result.step_ns = offset_ns - *previous_offset_ns_;
        result.status = classify(*previous_offset_ns_, offset_ns, elapsed_ns, policy_);
        if (result.step_ns <= max_step_ns && result.step_ns >= -max_step_ns) {
            result.rate_ppm = evaluate_rate_ppm(result.step_ns, elapsed_ns, policy_);
        }
    }
    previous_offset_ns_ = offset_ns;
    previous_mono_ns_ = monotonic_ns;
    return result;
}

} // namespace nexigen::chrono
