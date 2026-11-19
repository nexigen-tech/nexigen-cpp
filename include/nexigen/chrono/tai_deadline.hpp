// nexigen::chrono — absolute TAI deadlines for `clock_nanosleep`.
//
// A TAI deadline is an absolute instant on `CLOCK_TAI`. On a kernel that
// exposes `CLOCK_TAI`, sleeping against it needs no conversion and never touches
// the hub or any published row; these helpers keep that property explicit and
// give tests one place to pin the `timespec` construction.
#pragma once

#include <cstdint>
#include <ctime>
#include <optional>

#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// @brief Convert an absolute TAI instant into a `timespec` deadline.
///
/// @param tai_ns Absolute `CLOCK_TAI` instant in nanoseconds; negative values
///               have no `timespec` representation before the epoch.
/// @return The deadline, or `std::nullopt` when `tai_ns` is negative or does
///         not fit `time_t`.
[[nodiscard]] inline std::optional<timespec> tai_deadline(std::int64_t tai_ns) noexcept {
    if (tai_ns < 0) {
        return std::nullopt;
    }
    timespec deadline{};
    deadline.tv_sec = static_cast<time_t>(tai_ns / 1'000'000'000LL);
    deadline.tv_nsec = static_cast<long>(tai_ns % 1'000'000'000LL);
    return deadline;
}

/// @brief Sleep until an absolute TAI deadline on `CLOCK_TAI`.
///
/// The call retries on `EINTR`, so a signal does not shorten the deadline.
///
/// @param tai_ns Absolute `CLOCK_TAI` instant in nanoseconds.
/// @return Success, `InvalidArgument` for a negative deadline, or
///         `SystemError` when the kernel clock is unavailable.
/// @note Requires `CLOCK_TAI`; a client that declared TAI must fail fast when
///       the kernel does not provide it.
[[nodiscard]] core::Status sleep_until_tai(std::int64_t tai_ns) noexcept;

} // namespace nexigen::chrono
