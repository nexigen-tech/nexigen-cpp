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

namespace {

/// @brief Classify one offset against the stored baseline and update it.
///
/// @param policy Thresholds.
/// @param previous_offset_ns Baseline offset, if any.
/// @param previous_mono_ns Baseline monotonic instant, if any.
/// @param offset_ns Current offset.
/// @param monotonic_ns Current monotonic instant.
/// @return The classification.
[[nodiscard]] TimeSyncSample classify_and_store(const TimeSyncPolicy& policy,
                                                std::optional<std::int64_t>& previous_offset_ns,
                                                std::optional<std::int64_t>& previous_mono_ns,
                                                std::int64_t offset_ns,
                                                std::int64_t monotonic_ns) noexcept {
    TimeSyncSample result{};
    result.offset_ns = offset_ns;
    if (!previous_offset_ns.has_value() || !previous_mono_ns.has_value()) {
        result.status = TimeSyncStatus::Ok;
    } else {
        const std::int64_t elapsed_ns = monotonic_ns - *previous_mono_ns;
        const std::int64_t max_step_ns = policy.max_step_ns < 0 ? 0 : policy.max_step_ns;
        result.step_ns = offset_ns - *previous_offset_ns;
        result.status =
            TimeSyncMonitor::classify(*previous_offset_ns, offset_ns, elapsed_ns, policy);
        if (result.step_ns <= max_step_ns && result.step_ns >= -max_step_ns) {
            result.rate_ppm = evaluate_rate_ppm(result.step_ns, elapsed_ns, policy);
        }
    }
    previous_offset_ns = offset_ns;
    previous_mono_ns = monotonic_ns;
    return result;
}

} // namespace

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
    return classify_and_store(policy_, previous_offset_ns_, previous_mono_ns_,
                              realtime_ns - monotonic_ns, monotonic_ns);
}

std::optional<TimeSyncSample> TimeSyncMonitor::sample(std::int64_t realtime_ns,
                                                      std::uint64_t monotonic_raw_ns) noexcept {
    if (monotonic_raw_ns > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    const std::int64_t monotonic_ns = static_cast<std::int64_t>(monotonic_raw_ns);
    if (previous_mono_ns_.has_value() && monotonic_ns < *previous_mono_ns_) {
        return std::nullopt;
    }
    return classify_and_store(policy_, previous_offset_ns_, previous_mono_ns_,
                              realtime_ns - monotonic_ns, monotonic_ns);
}

} // namespace nexigen::chrono
