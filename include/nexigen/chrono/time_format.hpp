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

namespace nexigen::chrono {

/// Broken-down UTC time.
struct Civil_time {
    std::int64_t year{};
    std::uint8_t month{};     ///< 1..12
    std::uint8_t day{};       ///< 1..31
    std::uint8_t hour{};      ///< 0..23
    std::uint8_t minute{};    ///< 0..59
    std::uint8_t second{};    ///< 0..59
    std::uint32_t nanosecond{}; ///< 0..999'999'999
};

/// Days since 1970-01-01 (floor division: negative utc_ns maps correctly).
[[nodiscard]] std::int64_t days_from_utc_ns(std::int64_t utc_ns) noexcept;

/// Full UTC decomposition of a nanosecond timestamp.
[[nodiscard]] Civil_time civil_from_utc_ns(std::int64_t utc_ns) noexcept;

/// Format exactly "YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ" (always 30 chars + NUL)
/// into buf. Years outside 0..9999 clamp to the field width (never overflow
/// the buffer — see test fixtures).
void format_utc_iso8601_ns(std::int64_t utc_ns, char (&buf)[31]) noexcept;

} // namespace nexigen::chrono
