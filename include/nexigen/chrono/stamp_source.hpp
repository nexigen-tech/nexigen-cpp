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

/// Producer-owned monotonic nanosecond clock. class (not struct): the fields
/// are implementation state; sample()/calibrate() are the whole interface.
class alignas(64) Stamp_source {
public:
    Stamp_source() noexcept = default;
    Stamp_source(const Stamp_source&) = delete;
    Stamp_source& operator=(const Stamp_source&) = delete;
    Stamp_source(Stamp_source&&) = delete;
    Stamp_source& operator=(Stamp_source&&) = delete;

    /// Cold path (initialize time): sample the tick↔nanosecond scale.
    /// Returns SystemError if either clock read fails. Non-x86: verifies the
    /// clock once and leaves the fast path disarmed. Defined in
    /// src/chrono/clock_calibration.cpp because it sleeps.
    [[nodiscard]] core::Status calibrate() noexcept;

    /// Hot path: next monotonic timestamp, non-decreasing per source object.
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
            if (ns < last_ns_) { ns = last_ns_; }
            last_ns_ = ns;
            return Timestamp{ns};
        }
#endif
        const core::Result<Timestamp> now = Timestamp::now();
        std::uint64_t ns = now ? now->nanoseconds() : last_ns_;
        if (ns < last_ns_) { ns = last_ns_; }
        last_ns_ = ns;
        return Timestamp{ns};
    }

    /// Cold-path diagnostic (mandate 9): calibration state of this producer-
    /// owned source — fast-path arm, anchor pair, scale, last emitted ns.
    /// Reads producer-owned plain state: call from the owning thread or during
    /// quiescent diagnostics only, never between sample() calls of another
    /// thread, and never on the sampling hot path (allocates a std::string).
    [[nodiscard]] std::string to_string() const {
        std::string out = calibrated_ ? "Stamp_source(calibrated=true, "
                                      : "Stamp_source(calibrated=false, ";
        out += "tsc_anchor=" + std::to_string(tsc_anchor_);
        out += ", mono_anchor_ns=" + std::to_string(mono_anchor_ns_);
        out += ", scale_ns_x2^20=" + std::to_string(scale_);
        out += ", last_ns=" + std::to_string(last_ns_) + ")";
        return out;
    }

private:
#if defined(__x86_64__)
    /// Re-anchor the (ticks, monotonic-ns) pair around `ticks` (already read).
    /// mono is sampled after the tick, so derived ns may lag by the clock-read
    /// latency — absorbed by the monotonic clamp below. On clock failure the
    /// old anchor stays (stale-but-valid conversion, retried next sample).
    void reanchor(std::uint64_t ticks) noexcept {
        ::timespec ts{};
        if (::clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0) {
            mono_anchor_ns_ = static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
                              static_cast<std::uint64_t>(ts.tv_nsec);
            tsc_anchor_ = ticks;
        }
    }

    static constexpr std::uint64_t default_reanchor_ticks = 1ULL << 42;
#endif

    std::uint64_t tsc_anchor_{};
    std::uint64_t mono_anchor_ns_{};
    std::uint64_t scale_{};          ///< ns per tick, fixed-point << 20
    std::uint64_t reanchor_ticks_ =
#if defined(__x86_64__)
        default_reanchor_ticks;
#else
        0; // unused on non-x86: the fast path never arms
#endif
    std::uint64_t last_ns_{};        ///< monotonic clamp
    bool calibrated_{};
};

static_assert(sizeof(Stamp_source) <= 64, "stamp source must own one cache line");

/// Snapshot the offset such that utc_ns ≈ monotonic_ns + offset (logging
/// design D10). Cold path (initialize / rotation boundary / flush hook): two
/// clock reads, no allocation. The producer never calls CLOCK_REALTIME —
/// conversion to UTC happens on the consumer thread through this offset
/// snapshot.
[[nodiscard]] core::Status sample_utc_offset(std::int64_t& offset_ns_out) noexcept;

/// Apply an offset snapshot: the consumer-side UTC time of a record stamp.
[[nodiscard]] inline std::int64_t utc_from_mono_ns(std::uint64_t mono_ns,
                                                   std::int64_t offset_ns) noexcept {
    return static_cast<std::int64_t>(mono_ns) + offset_ns;
}

/// Free helper (mandate 9): mirrors Stamp_source::to_string for generic call sites.
[[nodiscard]] inline std::string to_string(const Stamp_source& source) {
    return source.to_string();
}

} // namespace nexigen::chrono
