// nexigen::chrono — hot-path monotonic stamp source (logging design.md D6,
// relocated out of nexigen::logging::detail by the extract-chrono-module change).
//
// Requirement 6 allows "monotonic Timestamp (or calibrated TSC)". A fresh
// clock_gettime per record is not an option: the vDSO call alone measures
// ~20-25 ns on older parts and would consume the entire 20 ns enqueue budget.
// On x86-64 the stamp therefore comes from __rdtsc() converted through a
// producer-owned calibration snapshot:
//
//   ns = mono_anchor_ns + ((ticks - tsc_anchor) * scale >> 20)
//
// with scale = (ns-per-tick << 20) sampled once at initialize(). The 64-bit
// product is kept far from overflow by re-anchoring the (ticks, mono) pair
// every reanchor_ticks ticks (~24 min at 3 GHz): the re-anchor is 2 clock
// reads amortized to zero for practical purposes.
//
// Monotonicity: the per-source `last_ns` clamp makes successive samples
// non-decreasing (spec logging/log-record), which also absorbs the sub-
// microsecond lag between the anchor pair's reads.
//
// On non-x86 targets `calibrated` stays false and every sample pays one
// Timestamp::now(); correctness first, the ns budget there is the platform's
// (logging design R3). A clock failure returns the previous stamp — the hot
// path never surfaces a stale-zero or a terminating call.
//
// The object is written exclusively by the producer thread (single-producer
// contract); alignment keeps it off neighbouring hot cells.
#pragma once

#include <cstdint>
#include <ctime>
#include <expected>
#include <string>

#include <nexigen/core/result.hpp>
#include <nexigen/chrono/timestamp.hpp>

#if defined(__x86_64__)
#include <x86intrin.h> // __rdtsc — an intrinsic, not a language extension
#endif

namespace nexigen::chrono {

/// @brief Producer-owned monotonic nanosecond clock with a calibrated TSC fast path.
///
/// One object serves one producer: every field is written by the owning thread
/// only (single-producer contract) and the object is `alignas(64)` so it never
/// shares a cache line with neighbouring hot cells. On x86-64 the fast path
/// converts `__rdtsc()` ticks through a producer-owned calibration pair
/// (`ns = mono_anchor_ns + ((ticks - tsc_anchor) * scale >> 20)`), re-anchoring
/// about every 2^42 ticks (~24 min at 3 GHz) to keep the 64-bit product far
/// from overflow. On non-x86 targets, or before calibration, every sample pays
/// one `Timestamp::now()`.
///
/// HOT/COLD SPLIT: `calibrate()` is the cold initialize-time operation that
/// samples the tick↔nanosecond scale; `sample()` is the only hot-path call and
/// performs no allocation, no lock, no atomic operation, no provider call, and
/// no wall-clock read. Diagnostics and calibration are cold-only.
///
/// @note Spec `chrono/stamp-source`, requirement "Calibrated monotonic stamp
///       source"; requirement "Stamp source calibration contract".
class alignas(64) StampSource {
public:
    /// @brief Construct an uncalibrated source that samples the direct clock.
    StampSource() noexcept = default;
    /// @brief Copying is deleted: the source is single-producer state.
    StampSource(const StampSource&) = delete;
    /// @brief Copy assignment is deleted: the source is single-producer state.
    StampSource& operator=(const StampSource&) = delete;
    /// @brief Moving is deleted: the source owns a thread-bound calibration.
    StampSource(StampSource&&) = delete;
    /// @brief Move assignment is deleted: the source owns a thread-bound calibration.
    StampSource& operator=(StampSource&&) = delete;

    /// @brief Cold path: sample the tick↔nanosecond scale for the fast path.
    ///
    /// Runs at initialize time, before any real-time use, and sleeps for the
    /// calibration window. Non-x86 targets verify the clock once and leave the
    /// fast path disarmed, so sampling stays on the direct-clock path. The body
    /// lives in `src/chrono/stamp_source.cpp` because it sleeps.
    ///
    /// @return Success when the fast path is armed (or verified unusable), or
    ///         `core::ErrorCode::SystemError` when a clock read fails or the
    ///         sampled window is degenerate; the source stays usable through
    ///         `sample()` either way.
    /// @note Spec `chrono/stamp-source`, requirement "Stamp source calibration
    ///       contract".
    [[nodiscard]] core::Status calibrate() noexcept;

    /// @brief Hot path: the next monotonic timestamp for this source.
    ///
    /// Successive samples from one object never decrease: a `last_ns_` clamp
    /// absorbs the sub-microsecond lag between the anchor pair's reads and the
    /// clock's own granularity. A clock read failure returns the previous
    /// stamp rather than a stale zero and never terminates.
    ///
    /// @return A non-decreasing `Timestamp` in the monotonic-raw domain.
    /// @note Spec `chrono/stamp-source`, requirement "Calibrated monotonic stamp
    ///       source"; steady-state sampling allocates nothing, takes no lock,
    ///       and never throws.
    [[nodiscard]] Timestamp sample() noexcept {
#if defined(__x86_64__)
        if (calibrated_) {
            const std::uint64_t ticks = __rdtsc();
            std::uint64_t delta = ticks - tsc_anchor_;
            if (delta > reanchor_ticks_) {
                reanchor(ticks);
                delta = ticks - tsc_anchor_; // reanchor pairs with this very tick
            }
            std::uint64_t ns = mono_anchor_ns_ + ((delta * scale_) >> 20);
            if (ns < last_ns_) {
                ns = last_ns_;
            }
            last_ns_ = ns;
            return Timestamp{ns};
        }
#endif
        const core::Result<Timestamp> now = Timestamp::now();
        std::uint64_t ns = now ? now->nanoseconds() : last_ns_;
        if (ns < last_ns_) {
            ns = last_ns_;
        }
        last_ns_ = ns;
        return Timestamp{ns};
    }

    /// @brief Render this source's calibration state as a diagnostic string.
    ///
    /// Reports the fast-path arm flag, the `(tsc_anchor, mono_anchor_ns)` pair,
    /// the fixed-point scale, and the last emitted nanosecond value, for
    /// post-mortem debugging of a mis-calibrated clock. Allocates a
    /// `std::string`, so it must run on the owning thread or during quiescent
    /// diagnostics — never between another thread's `sample()` calls and never
    /// on the sampling hot path.
    ///
    /// @return The rendered text, stating whether calibration succeeded.
    /// @note Spec `chrono/stamp-source`, requirement "Stamp source diagnostic
    ///       string"; requirement "Stamp source dump content"; project
    ///       mandate 9.
    [[nodiscard]] std::string to_string() const {
        std::string out =
            calibrated_ ? "StampSource(calibrated=true, " : "StampSource(calibrated=false, ";
        out += "tsc_anchor=" + std::to_string(tsc_anchor_);
        out += ", mono_anchor_ns=" + std::to_string(mono_anchor_ns_);
        out += ", scale_ns_x2^20=" + std::to_string(scale_);
        out += ", last_ns=" + std::to_string(last_ns_) + ")";
        return out;
    }

private:
#if defined(__x86_64__)
    /// @brief Re-anchor the `(ticks, monotonic-ns)` pair around `ticks`.
    ///
    /// `ticks` was already read by the caller. The monotonic clock is sampled
    /// after that read, so derived nanoseconds may lag by the clock-read
    /// latency; the `last_ns_` clamp absorbs it. On clock failure the old
    /// anchor pair stays (stale-but-valid conversion, retried on the next
    /// sample). Amortized cold work: it runs once per re-anchor window.
    ///
    /// @param ticks Cycle-counter value to pair with the fresh monotonic read.
    /// @note Spec `chrono/stamp-source`, requirement "Calibrated monotonic stamp
    ///       source".
    void reanchor(std::uint64_t ticks) noexcept {
        ::timespec ts{};
        if (::clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0) {
            mono_anchor_ns_ = static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
                              static_cast<std::uint64_t>(ts.tv_nsec);
            tsc_anchor_ = ticks;
        }
    }

    /// @brief Default re-anchor window in ticks (~2^42, about 24 min at 3 GHz).
    static constexpr std::uint64_t default_reanchor_ticks = 1ULL << 42;
#endif

    std::uint64_t tsc_anchor_{};     ///< TSC value paired with the monotonic anchor.
    std::uint64_t mono_anchor_ns_{}; ///< Monotonic-raw nanoseconds at the anchor.
    std::uint64_t scale_{};          ///< Fixed-point nanoseconds per tick (`<< 20`).
    std::uint64_t reanchor_ticks_ =
#if defined(__x86_64__)
        default_reanchor_ticks;
#else
        0; // unused on non-x86: the fast path never arms
#endif
    std::uint64_t last_ns_{}; ///< Monotonic clamp; last emitted nanosecond value.
    bool calibrated_{};       ///< True once `calibrate()` armed the fast path.
};

static_assert(sizeof(StampSource) <= 64, "stamp source must own one cache line");

/// @brief Free-helper mirror of `StampSource::to_string`.
///
/// @param source Stamp source to render.
/// @return The same text as `source.to_string()`.
/// @note Spec `chrono/stamp-source`, requirement "Stamp source diagnostic
///       string"; project mandate 9.
[[nodiscard]] inline std::string to_string(const StampSource& source) {
    return source.to_string();
}

} // namespace nexigen::chrono
