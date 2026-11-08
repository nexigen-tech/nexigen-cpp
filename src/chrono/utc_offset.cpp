// nexigen::chrono — cold-path UTC-offset snapshot (logging design.md D10;
// spec chrono/stamp-source "UTC offset snapshot calibration").
#include <cstdint>
#include <ctime>

#include <nexigen/chrono/utc_offset.hpp>

namespace nexigen::chrono {

core::Status sample_utc_offset(std::int64_t& offset_ns_out) noexcept {
    // (CLOCK_REALTIME - CLOCK_MONOTONIC_RAW) at one instant: the consumer's
    // monotonic→UTC conversion factor, re-sampled at init / rotation / flush
    // so wall time cannot drift on long runs (design D10). NTP steps show up
    // as a jump in the offset, never as record-order distortion.
    ::timespec realtime{};
    ::timespec monotonic{};
    if (::clock_gettime(CLOCK_REALTIME, &realtime) != 0 ||
        ::clock_gettime(CLOCK_MONOTONIC_RAW, &monotonic) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    const std::int64_t rt_ns = static_cast<std::int64_t>(realtime.tv_sec) * 1'000'000'000LL +
                               static_cast<std::int64_t>(realtime.tv_nsec);
    const std::int64_t mono_ns = static_cast<std::int64_t>(monotonic.tv_sec) * 1'000'000'000LL +
                                 static_cast<std::int64_t>(monotonic.tv_nsec);
    offset_ns_out = rt_ns - mono_ns;
    return {};
}

} // namespace nexigen::chrono
