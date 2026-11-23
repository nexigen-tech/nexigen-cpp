#include "time_formatter.hpp"

#include <cinttypes>
#include <cstdio>
#include <optional>

#include <nexigen/chrono/civil_time.hpp>
#include <nexigen/chrono/time_base_clock.hpp>
#include <nexigen/chrono/utc_offset.hpp>

namespace nexigen::logview {

std::uint64_t TimeFormatter::magnitude(std::int64_t value) noexcept {
    if (value >= 0) {
        return static_cast<std::uint64_t>(value);
    }
    return static_cast<std::uint64_t>(-(value + 1)) + 1ULL;
}

bool TimeFormatter::is_anomalous(std::int64_t delta_ns, std::int64_t threshold_ns) noexcept {
    const std::uint64_t threshold = magnitude(threshold_ns);
    return magnitude(delta_ns) > threshold;
}

FormattedTime TimeFormatter::format_timestamp(std::uint64_t stamp_ns, TimestampMode mode,
                                              const TimeContext& context) noexcept {
    FormattedTime out{};
    if (mode == TimestampMode::SinceBoot) {
        const std::uint64_t seconds = stamp_ns / 1'000'000'000ULL;
        const std::uint64_t nanos = stamp_ns % 1'000'000'000ULL;
        const std::uint64_t days = seconds / 86'400ULL;
        const std::uint64_t within = seconds % 86'400ULL;
        if (days > 0U) {
            static_cast<void>(
                std::snprintf(out.text.data(), out.text.size(),
                              "%" PRIu64 "d %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%09" PRIu64,
                              days, within / 3600U, (within / 60U) % 60U, within % 60U, nanos));
        } else {
            static_cast<void>(std::snprintf(out.text.data(), out.text.size(),
                                            "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%09" PRIu64,
                                            within / 3600U, (within / 60U) % 60U, within % 60U,
                                            nanos));
        }
        return out;
    }
    if (mode == TimestampMode::RelativeT0) {
        if (!context.anchor_ns.has_value()) {
            static_cast<void>(std::snprintf(out.text.data(), out.text.size(), "-"));
            return out;
        }
        const std::int64_t delta =
            static_cast<std::int64_t>(stamp_ns) - static_cast<std::int64_t>(*context.anchor_ns);
        return format_delta(delta);
    }

    if (context.time_base_attached) {
        const auto utc =
            nexigen::chrono::TimeBaseClock::utc_from_canonical(static_cast<std::int64_t>(stamp_ns));
        if (!utc.has_value()) {
            out.unknown = true;
            out.calibrated = false;
            static_cast<void>(
                std::snprintf(out.text.data(), out.text.size(), "%s", "no time base"));
            return out;
        }
        out.calibrated = true;
        char iso[31]{};
        nexigen::chrono::format_utc_iso8601_ns(*utc, iso);
        static_cast<void>(std::snprintf(out.text.data(), out.text.size(), "%s", iso));
        return out;
    }
    const std::int64_t utc = nexigen::chrono::utc_from_mono_ns(stamp_ns, context.utc_offset_ns);
    char iso[31]{};
    nexigen::chrono::format_utc_iso8601_ns(utc, iso);
    static_cast<void>(std::snprintf(out.text.data(), out.text.size(), "%s", iso));
    return out;
}

FormattedTime TimeFormatter::format_delta(std::int64_t delta_ns) noexcept {
    FormattedTime out{};
    out.negative = delta_ns < 0;
    const std::uint64_t mag = magnitude(delta_ns);
    const char sign = out.negative ? '-' : '+';
    if (mag < 1'000U) {
        static_cast<void>(
            std::snprintf(out.text.data(), out.text.size(), "%c%" PRIu64 " ns", sign, mag));
    } else if (mag < 1'000'000U) {
        static_cast<void>(std::snprintf(out.text.data(), out.text.size(),
                                        "%c%" PRIu64 ".%03" PRIu64 " us", sign, mag / 1'000U,
                                        mag % 1'000U));
    } else if (mag < 1'000'000'000U) {
        static_cast<void>(std::snprintf(out.text.data(), out.text.size(),
                                        "%c%" PRIu64 ".%03" PRIu64 " ms", sign, mag / 1'000'000U,
                                        (mag % 1'000'000U) / 1'000U));
    } else {
        static_cast<void>(std::snprintf(out.text.data(), out.text.size(),
                                        "%c%" PRIu64 ".%09" PRIu64 " s", sign, mag / 1'000'000'000U,
                                        mag % 1'000'000'000U));
    }
    return out;
}

} // namespace nexigen::logview
