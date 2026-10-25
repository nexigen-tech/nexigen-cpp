// nexigen::chrono — civil-time implementation (logging design.md D10/D11,
// relocated out of src/logging/ by the extract-chrono-module change): see
// chrono/time_format.hpp for the contract. Pure integer arithmetic; no
// locale, no timezone state, no allocation.
#include <cstdint>

#include <nexigen/chrono/time_format.hpp>

namespace nexigen::chrono {
namespace {

constexpr std::int64_t ns_per_day = 86'400'000'000'000LL;
constexpr std::int64_t ns_per_sec = 1'000'000'000LL;

/// Floor division (C++'s / truncates toward zero; we need floor for the
/// pre-epoch negative domain).
[[nodiscard]] constexpr std::int64_t floordiv(std::int64_t a, std::int64_t b) noexcept {
    const std::int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

/// Howard Hinnant's civil_from_days (days since 1970-01-01 → y/m/d).
void civil_from_days(std::int64_t z, std::int64_t& year, std::uint8_t& month,
                     std::uint8_t& day) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<std::uint32_t>(z - era * 146097);          // [0, 146096]
    const std::uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);      // [0, 365]
    const std::uint32_t mp = (5 * doy + 2) / 153;                           // [0, 11]
    const std::uint32_t d = doy - (153 * mp + 2) / 5 + 1;                   // [1, 31]
    const std::uint32_t m = (mp < 10) ? (mp + 3u) : (mp - 9u);              // [1, 12]
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
    year = y;
    month = static_cast<std::uint8_t>(m);
    day = static_cast<std::uint8_t>(d);
}

/// Write `value` right-justified into buf[pos..pos+width), zero-padded.
/// Assumes value fits in width (callers clamp).
void put_fixed(char (&buf)[31], std::size_t& pos, std::uint64_t value, std::size_t width) noexcept {
    for (std::size_t i = width; i-- > 0;) {
        buf[pos + i] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    }
    pos += width;
}

} // namespace

std::int64_t days_from_utc_ns(std::int64_t utc_ns) noexcept {
    return floordiv(utc_ns, ns_per_day);
}

Civil_time civil_from_utc_ns(std::int64_t utc_ns) noexcept {
    Civil_time ct{};
    const std::int64_t days = days_from_utc_ns(utc_ns);
    const std::int64_t sec_of_day = floordiv(utc_ns - days * ns_per_day, ns_per_sec);
    const std::uint32_t ns_in_sec =
        static_cast<std::uint32_t>(utc_ns - days * ns_per_day - sec_of_day * ns_per_sec);
    civil_from_days(days, ct.year, ct.month, ct.day);
    ct.hour = static_cast<std::uint8_t>(sec_of_day / 3600);
    ct.minute = static_cast<std::uint8_t>((sec_of_day % 3600) / 60);
    ct.second = static_cast<std::uint8_t>(sec_of_day % 60);
    ct.nanosecond = ns_in_sec;
    return ct;
}

void format_utc_iso8601_ns(std::int64_t utc_ns, char (&buf)[31]) noexcept {
    const Civil_time ct = civil_from_utc_ns(utc_ns);
    std::size_t pos = 0;
    // Year: the int64-ns domain reaches roughly 1678..2262 — always 4 digits;
    // clamp defensively so a hostile input can never shift the field layout.
    std::uint64_t year = 0ULL;
    if (ct.year > 0) {
        year = (ct.year < 10'000) ? static_cast<std::uint64_t>(ct.year) : 9999ULL;
    }
    put_fixed(buf, pos, year, 4);
    buf[pos++] = '-';
    put_fixed(buf, pos, ct.month, 2);
    buf[pos++] = '-';
    put_fixed(buf, pos, ct.day, 2);
    buf[pos++] = 'T';
    put_fixed(buf, pos, ct.hour, 2);
    buf[pos++] = ':';
    put_fixed(buf, pos, ct.minute, 2);
    buf[pos++] = ':';
    put_fixed(buf, pos, ct.second, 2);
    buf[pos++] = '.';
    put_fixed(buf, pos, ct.nanosecond, 9);
    buf[pos++] = 'Z';
    buf[pos] = '\0';
}

} // namespace nexigen::chrono
