// nexigen::synchro — raw-monotonic clock helpers (change add-synchro-event-sync;
// spec synchro/protocol; design D2).
//
// The whole synchronization layer lives in CLOCK_MONOTONIC_RAW: schedule
// instants, payload scheduled times, header timestamps, and consumer receive
// times. These helpers keep one allocation-free, exception-free conversion site
// so no endpoint re-implements the error mapping.
#pragma once

#include <cerrno>
#include <cstdint>
#include <ctime>

#include <nexigen/synchro/detail/sync_faults.hpp>
#include <nexigen/synchro/sync_error.hpp>

namespace nexigen::synchro::detail {

/// @brief Read the current `CLOCK_MONOTONIC_RAW` nanosecond count.
///
/// Performs one `clock_gettime` call through the vDSO, allocates nothing, and
/// takes no lock. A failed read is mapped to `SyncErrorKind::Clock` with
/// `core = SystemError` and the captured `errno`.
///
/// @return Current raw nanoseconds, or a clock error.
/// @note Spec `synchro/protocol`, requirement "Deterministic phase schedule";
///       design D2.
[[nodiscard]] inline SyncResult<std::uint64_t> raw_now_ns() noexcept {
    if (clock_failure_armed()) {
        return std::unexpected(make_os_error(SyncErrorKind::Clock, EIO));
    }
    ::timespec ts{};
    if (::clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        return std::unexpected(make_os_error(SyncErrorKind::Clock, errno));
    }
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

/// @brief Signed difference between two raw nanosecond instants.
///
/// Returns `lhs - rhs` in the signed range, clamping instead of wrapping when
/// the true difference does not fit `std::int64_t` (a plausible value only for
/// absurd schedule times, but the clamp keeps latency telemetry finite).
///
/// @param lhs Later instant.
/// @param rhs Earlier instant.
/// @return Signed nanosecond difference, saturated to the `int64_t` range.
/// @note Spec `synchro/consumer`, requirement "Late-event classification".
[[nodiscard]] constexpr std::int64_t signed_delta_ns(std::uint64_t lhs,
                                                     std::uint64_t rhs) noexcept {
    constexpr std::uint64_t limit = static_cast<std::uint64_t>(INT64_MAX);
    if (lhs >= rhs) {
        const std::uint64_t delta = lhs - rhs;
        return delta > limit ? INT64_MAX : static_cast<std::int64_t>(delta);
    }
    const std::uint64_t delta = rhs - lhs;
    if (delta >= limit) {
        return INT64_MIN + 1; // -INT64_MAX, never UB on negation
    }
    return -static_cast<std::int64_t>(delta);
}

} // namespace nexigen::synchro::detail
