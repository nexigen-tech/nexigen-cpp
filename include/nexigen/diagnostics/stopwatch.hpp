// nexigen::diagnostics — the core interval timer (design.md D2–D4; spec
// diagnostics/stopwatch).
//
// Stopwatch measures wall intervals in the raw monotonic domain with a fully
// pinned layout: one cache line, trivially copyable, no atomics, no locks, no
// heap. Every sampling operation costs exactly one stamp — Timestamp::now()
// (~20-25 ns vDSO) by default, or a caller-owned nexigen::chrono::Stamp_source
// (~3-5 ns calibrated TSC) when injected at construction. Samples of either
// source are monotone by contract, so NO second monotonic clamp is kept here
// (design D3): elapsed arithmetic is plain unsigned subtraction and can never
// wrap backwards.
//
// HOT PATH (design D4 accounting): start/stop = one stamp + O(1) register
// work; restart closes and re-opens at the very same stamp (zero time gap
// between intervals); lap re-anchors at its stamp; elapsed queries in running
// state sample once, in stopped/idle state are pure arithmetic.
//
// Clock-failure policy (design D3): Timestamp::now() is Result-based, and
// under the noexcept void signatures a transition commits only on a good
// sample — state and counters never lie, is_running() and frozen readings
// make the failure observable, and no fabricated value ever reaches the
// caller. Stamp_source::sample() self-falls-back internally and never fails,
// so a calibrated-source stopwatch exercises no failure branch on x86-64.
//
// Serialization (mandate 9): cold-path member + free to_string() renders the
// live state; definitions live in src/diagnostics/stopwatch_format.cpp — the
// one cold unit this module compiles (design D1).
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/diagnostics/stopwatch_state.hpp>

namespace nexigen::diagnostics {

/// Value companion to the scalar lap counters (design D6; spec
/// diagnostics/stopwatch "LapSnapshot captures a lap measurement"): one lap's
/// data — the 1-based number of the most recent lap, its duration, the
/// elapsed total and the state observed at the snapshot. State-first field
/// order keeps the struct at 24 bytes (uint8 state, uint32 lap_number, two
/// uint64 counts); trivially copyable, copyable in O(1). Cold-path to_string
/// lives in src/diagnostics/stopwatch_format.cpp (mandate 9).
struct LapSnapshot {
    StopwatchState state;      ///< The lifecycle state observed at the snapshot.
    std::uint32_t lap_number;  ///< 1-based index of the most recent lap (0 = none).
    std::uint64_t lap_ns;      ///< Duration of that most recent lap (0 = none).
    std::uint64_t total_ns;    ///< Elapsed nanoseconds at the snapshot.

    /// Cold-path diagnostic (mandate 9; spec "Diagnostic rendering").
    [[nodiscard]] std::string to_string() const;

    /// All fields compare — snapshots are plain values.
    [[nodiscard]] friend constexpr bool operator==(const LapSnapshot&,
                                                   const LapSnapshot&) = default;
};

/// Free helper (mandate 9): mirrors LapSnapshot::to_string.
[[nodiscard]] inline std::string to_string(const LapSnapshot& snapshot) {
    return snapshot.to_string();
}

/// An interval timer: measure, split, budget — the diagnostic stopwatch.
/// Single-thread measuring domain by design (fresh instance per thread, no
/// atomics — design D2/D3 concurrency analysis); alignas(64) keeps it off
/// neighbouring hot lines whenever it is embedded in loop-local structures.
/// Copies are rare but legal: they produce a consistent point-in-time
/// snapshot (anchor and accumulated total move together).
class alignas(64) Stopwatch {
public:
    /// The `min_lap_ns()` no-lap sentinel: queryable all-ones value that
    /// distinguishes "no lap recorded" from a (physically impossible) zero.
    static constexpr std::uint64_t no_lap_value{0xFFFF'FFFF'FFFF'FFFFULL};
    /// Default clock domain: every stamp is Timestamp::now()
    /// (CLOCK_MONOTONIC_RAW).
    Stopwatch() noexcept = default;

    /// Calibrated-TSC domain: every stamp rides the caller-owned Stamp_source
    /// (borrowed, NOT owned — it must outlive this stopwatch). A null pointer
    /// selects the default clock domain.
    explicit Stopwatch(chrono::Stamp_source* source) noexcept : source_{source} {}

    /// The injected stamp source (nullptr = the default clock domain).
    [[nodiscard]] constexpr chrono::Stamp_source* source() const noexcept { return source_; }

    // -- State control (design D4; spec "Stopwatch state machine ...")

    /// Start or resume: from Idle begins a fresh interval; from Stopped
    /// resumes with the accumulated total kept; a no-op while already
    /// Running. The transition commits only on a good sample.
    void start() noexcept {
        if (state_ == StopwatchState::Running) { return; }
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) { return; } // uncommitted (design D3)
        anchor_ = chrono::Timestamp{stamp};
        lap_anchor_ = anchor_; // first lap of the run measures from this stamp
        state_ = StopwatchState::Running;
    }

    /// Stop: closes the active interval into the accumulated total and
    /// freezes the reading; a no-op when not already Running. Lap statistics
    /// are left untouched — the tail after the last lap() is deliberately not
    /// a lap (laps are explicit only).
    void stop() noexcept {
        if (state_ != StopwatchState::Running) { return; }
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) { return; } // uncommitted (design D3)
        accumulated_ns_ += stamp - anchor_.nanoseconds();
        state_ = StopwatchState::Stopped;
    }

    /// Reset: from any state back to Idle — accumulated time gone, lap
    /// statistics back to their no-lap defaults. Never samples.
    void reset() noexcept {
        accumulated_ns_ = 0U;
        last_lap_ns_ = 0U;
        min_lap_ns_ = no_lap_value;
        max_lap_ns_ = 0U;
        lap_count_ = 0U;
        state_ = StopwatchState::Idle;
    }

    /// Zero-gap restart (spec "Zero-gap restart atomicity"): returns the
    /// total elapsed at the switch and re-anchors the next interval at that
    /// very same stamp — the Running path takes one stamp that serves both
    /// the close and the fresh open, so no wall time passes uncounted between
    /// the intervals. From Stopped the frozen accumulated total is returned
    /// and zeroed, then a fresh interval opens at a new sample; from Idle the
    /// return is zero and a fresh interval opens. Lap statistics are kept.
    /// Always ends Running. Uncommitted on a failed sample (nothing mutated).
    [[nodiscard]] std::uint64_t restart() noexcept {
        if (state_ == StopwatchState::Running) {
            std::uint64_t stamp = 0U;
            if (!sample_ok_(stamp)) { return accumulated_ns_; } // uncommitted
            const std::uint64_t total =
                accumulated_ns_ + (stamp - anchor_.nanoseconds());
            accumulated_ns_ = 0U;
            anchor_ = chrono::Timestamp{stamp}; // same stamp as the close
            lap_anchor_ = anchor_;
            return total;
        }
        const std::uint64_t total =
            state_ == StopwatchState::Stopped ? accumulated_ns_ : 0U;
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) { return total; } // uncommitted
        accumulated_ns_ = 0U;
        anchor_ = chrono::Timestamp{stamp};
        lap_anchor_ = anchor_;
        state_ = StopwatchState::Running;
        return total;
    }

    // -- Lap / split timing (spec "O(1) lap and split tracking")

    /// Split time: nanoseconds since the previous lap() (or since start()
    /// for the first lap of the run) without stopping the stopwatch; the lap
    /// anchor re-anchors at its own stamp while the run anchor keeps the
    /// whole-run reading (spec line 59: elapsed stays the sum of stage laps
    /// plus the live tail). A no-op returning 0 while not Running (no
    /// phantom laps, ever).
    [[nodiscard]] std::uint64_t lap() noexcept {
        if (state_ != StopwatchState::Running) { return 0U; }
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) { return 0U; } // uncommitted, no phantom lap
        const std::uint64_t duration = stamp - lap_anchor_.nanoseconds();
        lap_anchor_ = chrono::Timestamp{stamp};
        last_lap_ns_ = duration;
        if (duration < min_lap_ns_) { min_lap_ns_ = duration; } // sentinel-aware
        if (duration > max_lap_ns_) { max_lap_ns_ = duration; }
        ++lap_count_;
        return duration;
    }

    /// Laps recorded since the last reset (`uint32_t`: >= 2^32 laps is
    /// unreachable at realistic lap rates; the increment is well-defined).
    [[nodiscard]] constexpr std::uint32_t lap_count() const noexcept { return lap_count_; }

    /// Most recent lap duration (0 before any lap).
    [[nodiscard]] constexpr std::uint64_t last_lap_ns() const noexcept { return last_lap_ns_; }

    /// Shortest lap so far; the `no_lap_value` sentinel before any lap, so a
    /// minimum is distinguishable from an empty set (and from zero).
    [[nodiscard]] constexpr std::uint64_t min_lap_ns() const noexcept { return min_lap_ns_; }

    /// Longest lap so far (0 before any lap).
    [[nodiscard]] constexpr std::uint64_t max_lap_ns() const noexcept { return max_lap_ns_; }

    /// Snapshot of the latest lap context (spec "Snapshotting does not mutate
    /// the stopwatch"): a pure read — total_ns is the elapsed at the very
    /// instant of the call, lap_number/lap_ns report the latest lap, state
    /// reports the lifecycle state. No sample mutates control state.
    [[nodiscard]] LapSnapshot lap_snapshot() const noexcept {
        return LapSnapshot{
            .state = state_,
            .lap_number = lap_count_,
            .lap_ns = last_lap_ns_,
            .total_ns = elapsed_ns(),
        };
    }

    // -- Elapsed queries (spec "Multi-unit elapsed queries do not mutate
    // state"): valid in Running (one stamp) and Stopped (pure arithmetic);
    // they mutate nothing. Idle reports zero. A clock failure freezes the
    // running reading at the accumulated total (design D3).

    /// Exact elapsed, nanoseconds; never negative, never silently wrapped.
    [[nodiscard]] std::uint64_t elapsed_ns() const noexcept {
        if (state_ == StopwatchState::Idle) { return 0U; }
        if (state_ == StopwatchState::Stopped) { return accumulated_ns_; }
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) { return accumulated_ns_; } // frozen read
        return accumulated_ns_ + (stamp - anchor_.nanoseconds());
    }

    /// Whole microseconds, truncated from the nanosecond total (integer form).
    [[nodiscard]] std::uint64_t elapsed_us_count() const noexcept {
        return elapsed_ns() / 1'000U;
    }

    /// Fractional microseconds, double precision, from the nanosecond total.
    [[nodiscard]] double elapsed_us() const noexcept {
        return static_cast<double>(elapsed_ns()) / 1'000.0;
    }

    /// Fractional milliseconds, double precision, from the nanosecond total.
    [[nodiscard]] double elapsed_ms() const noexcept {
        return static_cast<double>(elapsed_ns()) / 1'000'000.0;
    }

    /// Fractional seconds, double precision, from the nanosecond total.
    [[nodiscard]] double elapsed_seconds() const noexcept {
        return static_cast<double>(elapsed_ns()) / 1'000'000'000.0;
    }

    // -- State readback

    [[nodiscard]] constexpr StopwatchState state() const noexcept { return state_; }

    [[nodiscard]] constexpr bool is_running() const noexcept {
        return state_ == StopwatchState::Running;
    }

    /// Cold-path diagnostic (mandate 9; never in hot paths): state name, the
    /// total elapsed and the lap counter. Defined in
    /// src/diagnostics/stopwatch_format.cpp.
    [[nodiscard]] std::string to_string() const;

private:
    /// The configured stamp sampler as a Result-free face (design D3): true
    /// and the raw count on a good sample; false on a clock failure. The
    /// injected source never fails (it self-falls-back internally).
    [[nodiscard]] bool sample_ok_(std::uint64_t& stamp_ns_out) const noexcept {
        if (source_ != nullptr) {
            stamp_ns_out = source_->sample().nanoseconds();
            return true;
        }
        const core::Result<chrono::Timestamp> stamp = chrono::Timestamp::now();
        if (!stamp.has_value()) { return false; } // branch, never .value()
        stamp_ns_out = stamp->nanoseconds();
        return true;
    }

    /// [Design D2 field order] 8-byte fields first, the uint32/uint8 tail
    /// last: 61 bytes of state; sizeof == 64 under alignas(64). Two anchors:
    /// `anchor_` is where the current run began (start/restart), feeding the
    /// elapsed/stop/restart arithmetic; `lap_anchor_` is where the current
    /// lap segment began (start/restart/lap), feeding split durations only —
    /// so elapsed keeps counting the whole run across laps (spec examples
    /// scenario "total elapsed matches the sum of stage laps").
    chrono::Timestamp anchor_{};         ///< Open-interval run anchor.
    chrono::Timestamp lap_anchor_{};     ///< Open-lap segment anchor.
    std::uint64_t accumulated_ns_{};     ///< Sum of closed intervals.
    std::uint64_t last_lap_ns_{};        ///< Most recent lap duration.
    std::uint64_t min_lap_ns_{no_lap_value}; ///< Shortest lap (sentinel = none).
    std::uint64_t max_lap_ns_{};         ///< Longest lap.
    chrono::Stamp_source* source_{};     ///< Borrowed sampler; null = default.
    std::uint32_t lap_count_{};          ///< Laps since the last reset.
    StopwatchState state_{};             ///< The lifecycle state (Idle).
};

/// Free helper (mandate 9): mirrors Stopwatch::to_string for generic/ADL call
/// sites.
[[nodiscard]] inline std::string to_string(const Stopwatch& stopwatch) {
    return stopwatch.to_string();
}

static_assert(sizeof(Stopwatch) == 64, "Stopwatch must own exactly one cache line");
static_assert(alignof(Stopwatch) == 64, "Stopwatch must be cache-line aligned");
static_assert(std::is_trivially_copyable_v<Stopwatch>,
              "Stopwatch must be a trivially copyable value type");
static_assert(sizeof(LapSnapshot) == 24, "LapSnapshot must stay a register-sized value bundle");
static_assert(std::is_trivially_copyable_v<LapSnapshot>,
              "LapSnapshot must be trivially copyable");
// Default member initializer pin: a fresh stopwatch sits in the no-lap state.
static_assert(([]() {
    constexpr Stopwatch fresh{};
    return fresh.min_lap_ns() == Stopwatch::no_lap_value && fresh.lap_count() == 0U;
})(), "a fresh stopwatch must report the no-lap sentinel");
} // namespace nexigen::diagnostics

/// Stopwatch formats as its diagnostic text (type-family formatter; built
/// from the cold-path rendering).
template <>
struct std::formatter<nexigen::diagnostics::Stopwatch>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::diagnostics::Stopwatch& stopwatch, Ctx& ctx) const {
        const std::string text = stopwatch.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
