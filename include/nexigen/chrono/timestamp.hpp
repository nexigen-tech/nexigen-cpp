// nexigen::chrono — monotonic nanosecond time source (logging design.md D2,
// relocated by the extract-chrono-module change).
//
// Timestamp is an 8-byte, trivially copyable value type wrapping a nanosecond
// count from CLOCK_MONOTONIC_RAW (the raw monotonic clock: unaffected by
// wall-clock adjustments, and by design not frequency-disciplined — fine for
// intervals and deadlines, not for absolute-time scheduling).
//
// HOT PATH: now() costs one clock_gettime call (vDSO on Linux/x86-64, no
// syscall trap), allocates nothing, locks nothing. A clock failure surfaces
// as Result<Timestamp>'s error arm (core::ErrorCode::SystemError) — never a
// silent zero and never a terminating call.
//
// Usage rule (see core/result.hpp): never call value() on the Result; branch
// on has_value() and use *res.
#pragma once

#include <compare>
#include <cstdint>
#include <ctime>
#include <expected>
#include <utility>

#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// A point in time: nanoseconds in the raw monotonic clock domain.
class Timestamp {
public:
    constexpr Timestamp() noexcept = default;

    /// Wraps a raw nanosecond count (unit-bearing: nanoseconds, not a bare integer).
    explicit constexpr Timestamp(std::uint64_t nanoseconds) noexcept : ns_{nanoseconds} {}

    /// Current instant from CLOCK_MONOTONIC_RAW; SystemError if the read fails.
    /// Successive calls on the same thread never decrease.
    [[nodiscard]] static core::Result<Timestamp> now() noexcept {
        ::timespec ts{};
        if (::clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        // tv_sec is signed time_t; the casts keep the math in uint64_t (no
        // -Wsign-conversion, no double promotion via the ULL literal).
        return Timestamp{static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
                         static_cast<std::uint64_t>(ts.tv_nsec)};
    }

    /// Raw nanosecond count.
    [[nodiscard]] constexpr std::uint64_t nanoseconds() const noexcept { return ns_; }

    /// Total ordering on the nanosecond count; == included (deadline compares).
    [[nodiscard]] constexpr auto operator<=>(const Timestamp&) const = default;

    /// Deadline arithmetic: instant + offset in nanoseconds.
    [[nodiscard]] constexpr Timestamp operator+(std::uint64_t offset) const noexcept {
        return Timestamp{ns_ + offset};
    }

    /// Deadline arithmetic: instant - offset in nanoseconds (saturating at 0).
    [[nodiscard]] constexpr Timestamp operator-(std::uint64_t offset) const noexcept {
        return ns_ >= offset ? Timestamp{ns_ - offset} : Timestamp{0};
    }

    /// Elapsed nanoseconds (a - b) as a Timestamp-valued delta; saturates at 0
    /// when `earlier` > this, because unsigned wrap-around would hand the caller
    /// a ~584-year interval instead of a detectable bug.
    [[nodiscard]] constexpr Timestamp operator-(const Timestamp& earlier) const noexcept {
        return ns_ >= earlier.ns_ ? Timestamp{ns_ - earlier.ns_} : Timestamp{0};
    }

private:
    std::uint64_t ns_{};
};

static_assert(sizeof(Timestamp) == 8, "Timestamp must stay a register-sized value type");
static_assert(std::is_trivially_copyable_v<Timestamp>, "Timestamp must be trivially copyable");

} // namespace nexigen::chrono
