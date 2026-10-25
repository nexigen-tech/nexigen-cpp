// nexigen::chrono — cold-path clock calibration (logging design.md D6/D10,
// relocated out of src/logging/ by the extract-chrono-module change).
//
// Stamp_source::calibrate(): sample the tick<->nanosecond scale once at
// logger initialization. Sleeps ~2 ms, so it lives in the compiled library
// (never inline, never on a hot path): the producer only ever runs the
// multiply-shift conversion and the rare re-anchor that stamp_source.hpp
// defines inline.
//
// Scale math: scale = (delta_ns << 20) / delta_ticks — fixed-point ns-per-
// tick with 20 fractional bits (~0.000001 precision). The shift guards below
// keep every product inside uint64_t for realistic 2 ms windows; anything
// weirder (clock failure, zero delta) refuses calibration and the caller
// falls back to the plain Timestamp::now() path.
#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>

#include <nexigen/core/result.hpp>
#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/chrono/timestamp.hpp>

namespace nexigen::chrono {

core::Status Stamp_source::calibrate() noexcept {
#if defined(__x86_64__)    // Anchor pair: ticks first, then the clock — the constant sub-microsecond
    // lag this introduces is absorbed by the per-source monotonic clamp.
    const std::uint64_t t1 = __rdtsc();
    const core::Result<Timestamp> m1 = Timestamp::now();
    if (!m1) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const core::Result<Timestamp> m2 = Timestamp::now();
    const std::uint64_t t2 = __rdtsc();
    if (!m2) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    const std::uint64_t delta_ticks = t2 - t1;
    const std::uint64_t delta_ns = m2->nanoseconds() - m1->nanoseconds();
    constexpr std::uint64_t max_shift_ns = std::numeric_limits<std::uint64_t>::max() >> 20;
    if (delta_ticks == 0U || delta_ns == 0U || delta_ns > max_shift_ns) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    scale_ = (delta_ns << 20U) / delta_ticks;
    tsc_anchor_ = t1;
    mono_anchor_ns_ = m1->nanoseconds();
    calibrated_ = true;
    return {};
#else
    // Non-x86: no cycle fast path; just prove the clock works once and leave
    // sample() on the Timestamp::now() branch.
    const core::Result<Timestamp> now = Timestamp::now();
    if (!now) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    calibrated_ = false;
    return {};
#endif
}

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
