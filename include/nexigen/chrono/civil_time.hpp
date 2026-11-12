// nexigen::chrono — civil-time + ISO-8601 formatting (logging design.md
// D10/D11, relocated out of nexigen::logging::detail by the
// extract-chrono-module change).
//
// The background consumer renders record timestamps WITHOUT libc time
// helpers: gmtime_r touches process-wide timezone state and timegm() is
// neither guaranteed allocation-free nor portable under our feature macros.
// civil_from_days is Howard Hinnant's proven integer algorithm — pure
// arithmetic, exact for the whole int64 ns domain we can reach.
//
// Output shape (spec logging/file-logger): "YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ"
// — 30 characters + NUL (31-byte buffer), fixed-width, nanosecond-accurate, UTC.
#pragma once

#include <cstdint>
#include <string>

namespace nexigen::chrono {

/// @brief Broken-down UTC civil time: calendar fields plus a nanosecond remainder.
///
/// Plain aggregate produced by `civil_from_utc_ns` and consumed by `to_string`
/// and by post-mortem diagnostics. It owns no resources, performs no clock
/// access and is trivially copyable; decomposition and rendering are cold-path
/// operations, never part of a producer or sink stamp path.
///
/// @note Spec `chrono/time-format`, requirement "UTC civil-time decomposition".
struct CivilTime {
    std::int64_t year{};        ///< Proleptic Gregorian year (may be negative).
    std::uint8_t month{};       ///< Month of year, 1..12.
    std::uint8_t day{};         ///< Day of month, 1..31.
    std::uint8_t hour{};        ///< Hour of day, 0..23.
    std::uint8_t minute{};      ///< Minute of hour, 0..59.
    std::uint8_t second{};      ///< Second of minute, 0..59.
    std::uint32_t nanosecond{}; ///< Nanosecond within the second, 0..999'999'999.

    /// @brief Render this broken-down time as a diagnostic string.
    ///
    /// Produces `YYYY-MM-DD HH:MM:SS.nnnnnnnnn UTC`; the exact decoration is
    /// diagnostics-only. Allocates a `std::string`, so it is a
    /// diagnostics/config/test utility and never a writer-loop call —
    /// `format_utc_iso8601_ns` is the allocation-free writer tool.
    ///
    /// @return The rendered text.
    /// @note Spec `chrono/time-format`, requirement "Civil time diagnostic
    ///       string"; project mandate 9.
    [[nodiscard]] std::string to_string() const {
        auto pad = [](std::uint64_t v, std::size_t width) {
            std::string s = std::to_string(v);
            if (s.size() < width) {
                s.insert(0, width - s.size(), '0');
            }
            return s;
        };
        // Year may be negative for pre-1970 instants; std::to_string handles sign,
        // pad only the digit run (width 4 minimum magnitude).
        const std::string year_s =
            year < 0 ? std::to_string(year) : pad(static_cast<std::uint64_t>(year), 4U);
        std::string out = year_s + '-' + pad(month, 2U) + '-' + pad(day, 2U);
        out += ' ' + pad(hour, 2U) + ':' + pad(minute, 2U) + ':' + pad(second, 2U);
        out += '.' + pad(nanosecond, 9U) + " UTC";
        return out;
    }
};

/// @brief Free-helper mirror of `CivilTime::to_string`.
///
/// @param ct Broken-down UTC time to render.
/// @return The same text as `ct.to_string()`.
/// @note Spec `chrono/time-format`, requirement "Civil time diagnostic
///       string"; project mandate 9.
[[nodiscard]] inline std::string to_string(const CivilTime& ct) {
    return ct.to_string();
}

/// @brief Whole days since 1970-01-01 for a signed UTC nanosecond count.
///
/// Uses floor division, so negative (pre-epoch) inputs map to the day before
/// 1970-01-01 instead of being truncated toward zero.
///
/// @param utc_ns Signed nanoseconds since the Unix epoch.
/// @return Days since 1970-01-01 (negative before the epoch).
/// @note Spec `chrono/time-format`, requirement "UTC civil-time decomposition".
[[nodiscard]] std::int64_t days_from_utc_ns(std::int64_t utc_ns) noexcept;

/// @brief Full UTC decomposition of a signed nanosecond timestamp.
///
/// Pure integer arithmetic (Howard Hinnant's `civil_from_days`), exact over
/// the reachable `int64` nanosecond domain; it never touches timezone, locale,
/// or libc time state and allocates nothing.
///
/// @param utc_ns Signed nanoseconds since the Unix epoch.
/// @return Broken-down UTC calendar fields for that instant.
/// @note Spec `chrono/time-format`, requirement "UTC civil-time decomposition".
[[nodiscard]] CivilTime civil_from_utc_ns(std::int64_t utc_ns) noexcept;

/// @brief Format a UTC nanosecond count as fixed-width ISO-8601.
///
/// Writes exactly `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` — 30 characters plus the
/// terminating NUL — into `buf`, without heap allocation, locks, or shared
/// state. Years outside 0..9999 clamp to the four-digit field width, so the
/// buffer is never overrun for any signed 64-bit input.
///
/// @param utc_ns Signed nanoseconds since the Unix epoch.
/// @param buf Caller-provided 31-byte buffer receiving the text and its NUL.
/// @note Spec `chrono/time-format`, requirement "Fixed-width ISO-8601
///       nanosecond formatting".
void format_utc_iso8601_ns(std::int64_t utc_ns, char (&buf)[31]) noexcept;

} // namespace nexigen::chrono
