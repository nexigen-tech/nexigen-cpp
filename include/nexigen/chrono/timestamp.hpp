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
#include <format>
#include <string>
#include <utility>

#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// @brief A point in time: nanoseconds in the raw monotonic clock domain.
///
/// 8-byte, trivially copyable value type wrapping a `CLOCK_MONOTONIC_RAW`
/// nanosecond count. It carries no ownership and no clock state; reading the
/// current instant costs one `clock_gettime` call (vDSO on Linux/x86-64),
/// allocates nothing, and takes no lock, so it is safe on producer and enqueue
/// hot paths. The raw monotonic clock is unaffected by wall-clock steps but is
/// deliberately not frequency-disciplined: use it for intervals, ordering and
/// deadlines, not absolute-time scheduling.
///
/// @note Spec `chrono/timestamp`, requirement "Nanosecond timestamp value
///       type"; requirement "Hot-path safety of timestamps".
class Timestamp {
public:
    /// @brief Construct the epoch value (zero nanoseconds).
    constexpr Timestamp() noexcept = default;

    /// @brief Wrap a raw nanosecond count.
    ///
    /// @param nanoseconds Unit-bearing count of nanoseconds, not a bare integer.
    explicit constexpr Timestamp(std::uint64_t nanoseconds) noexcept : ns_{nanoseconds} {}

    /// @brief Read the current instant from `CLOCK_MONOTONIC_RAW`.
    ///
    /// Successive reads on the same thread never decrease. A failed clock read
    /// surfaces in the error arm; the call never throws and never terminates.
    ///
    /// @return The current timestamp on success, or `core::ErrorCode::SystemError`
    ///         when the clock read fails.
    /// @note Spec `chrono/timestamp`, requirement "Monotonic clock sourcing";
    ///       branch on the result, never call `value()`.
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

    /// @brief Raw nanosecond count since the monotonic-raw epoch.
    ///
    /// @return The stored count, without unit conversion.
    [[nodiscard]] constexpr std::uint64_t nanoseconds() const noexcept { return ns_; }

    /// @brief Total ordering on the nanosecond count.
    ///
    /// Equality is part of the generated comparison, so deadline checks and
    /// map/set use resolve against the stored count alone.
    ///
    /// The right-hand operand is compared by its stored nanosecond count.
    ///
    /// @return The synthesized comparison category.
    /// @note Spec `chrono/timestamp`, requirement "Timestamp arithmetic".
    [[nodiscard]] constexpr auto operator<=>(const Timestamp&) const = default;

    /// @brief Deadline arithmetic: instant plus an offset.
    ///
    /// @param offset Nanoseconds to add.
    /// @return A new timestamp advanced by `offset`; wraps on `uint64_t`
    ///         overflow, which callers avoid by keeping offsets small.
    [[nodiscard]] constexpr Timestamp operator+(std::uint64_t offset) const noexcept {
        return Timestamp{ns_ + offset};
    }

    /// @brief Deadline arithmetic: instant minus an offset, saturating at zero.
    ///
    /// @param offset Nanoseconds to subtract.
    /// @return The advanced timestamp, or the epoch value when `offset` exceeds
    ///         the stored count.
    [[nodiscard]] constexpr Timestamp operator-(std::uint64_t offset) const noexcept {
        return ns_ >= offset ? Timestamp{ns_ - offset} : Timestamp{0};
    }

    /// @brief Elapsed nanoseconds between two instants, saturating at zero.
    ///
    /// `earlier > *this` yields zero instead of an unsigned wrap-around
    /// (~584 years) so a reversed subtraction stays a detectable bug.
    ///
    /// @param earlier Timestamp that should precede this one.
    /// @return The non-negative difference in nanoseconds as a Timestamp-valued
    ///         delta.
    /// @note Spec `chrono/timestamp`, requirement "Timestamp arithmetic",
    ///       scenario "Saturating backwards difference".
    [[nodiscard]] constexpr Timestamp operator-(const Timestamp& earlier) const noexcept {
        return ns_ >= earlier.ns_ ? Timestamp{ns_ - earlier.ns_} : Timestamp{0};
    }

    /// @brief Render this instant as a diagnostic string.
    ///
    /// Shows whole monotonic seconds with a nine-digit nanosecond remainder,
    /// for example `Timestamp(1.000000001s monotonic)`. Allocates a
    /// `std::string`, so it is a diagnostics/config utility and never a
    /// hot-path call.
    ///
    /// @return The rendered text.
    /// @note Spec `chrono/timestamp`, requirement "Timestamp diagnostic
    ///       string"; project mandate 9.
    [[nodiscard]] std::string to_string() const {
        std::string frac = std::to_string(ns_ % 1'000'000'000ULL);
        const std::size_t digits = frac.size();
        if (digits < 9U) {
            frac.insert(0, 9U - digits, '0');
        }
        return "Timestamp(" + std::to_string(ns_ / 1'000'000'000ULL) + '.' + frac + "s monotonic)";
    }

private:
    std::uint64_t ns_{};
};

/// @brief Free-helper mirror of `Timestamp::to_string`.
///
/// @param ts Timestamp to render.
/// @return The same text as `ts.to_string()`.
/// @note Spec `chrono/timestamp`, requirement "Timestamp diagnostic string";
///       project mandate 9.
[[nodiscard]] inline std::string to_string(const Timestamp& ts) {
    return ts.to_string();
}

static_assert(sizeof(Timestamp) == 8, "Timestamp must stay a register-sized value type");
static_assert(std::is_trivially_copyable_v<Timestamp>, "Timestamp must be trivially copyable");

} // namespace nexigen::chrono

/// @brief `std::formatter` specialization rendering the raw nanosecond count.
///
/// Formats through `std::formatter<std::uint64_t>`, so integer fill, width and
/// alignment rules apply. Required because the logging `Packable` surface
/// accepts timestamps and `std::format_string<Timestamp>` checks formattability
/// at the call site.
///
/// @note Spec `chrono/timestamp`, requirement "Nanosecond timestamp value
///       type"; project mandate 11.
template <> struct std::formatter<nexigen::chrono::Timestamp> : std::formatter<std::uint64_t> {
    /// @brief Format a timestamp as its raw nanosecond count.
    ///
    /// @tparam Ctx Format context type carrying the parsed integer specifiers.
    /// @param ts Timestamp to format.
    /// @param ctx Format context; the inherited integer fill/align rules apply.
    /// @return Iterator past the last written character.
    /// @note Project mandate 11 (`std::formatter`).
    template <class Ctx>
    constexpr auto format(const nexigen::chrono::Timestamp& ts, Ctx& ctx) const {
        return std::formatter<std::uint64_t>::format(ts.nanoseconds(), ctx);
    }
};
