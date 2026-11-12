// nexigen::diagnostics — the core interval timer (design.md D2–D4; spec
// diagnostics/stopwatch).
//
// Stopwatch measures wall intervals in the raw monotonic domain with a fully
// pinned layout: one cache line, trivially copyable, no atomics, no locks, no
// heap. Every sampling operation costs exactly one stamp — Timestamp::now()
// (~20-25 ns vDSO) by default, or a caller-owned nexigen::chrono::StampSource
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
// caller. StampSource::sample() self-falls-back internally and never fails,
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
#include <nexigen/diagnostics/lap_snapshot.hpp>

namespace nexigen::diagnostics {

// `LapSnapshot` (design D6; spec diagnostics/stopwatch "LapSnapshot captures a
// lap measurement") is the value companion to the scalar lap counters: one lap's
// number, duration, elapsed total and observed state, declared in
// lap_snapshot.hpp and returned by `Stopwatch::lap_snapshot()`. Its cold-path
// to_string lives in src/diagnostics/stopwatch_format.cpp (mandate 9).

/// @brief An interval timer: measure, split, budget — the diagnostic stopwatch.
///
/// Models the three-state lifecycle `Idle`/`Running`/`Stopped` with
/// accumulated-interval resume, a zero-gap `restart()`, and O(1) lap statistics.
/// It takes one timestamp per sampling operation: `Timestamp::now()`
/// (`CLOCK_MONOTONIC_RAW`) by default, or a caller-owned
/// `nexigen::chrono::StampSource` (calibrated TSC) borrowed at construction.
///
/// Single-thread measuring domain by design: no atomics, no locks, no heap; give
/// each thread its own instance. `alignas(64)` keeps it off neighbouring hot
/// lines whenever it is embedded in loop-local structures. Copies are rare but
/// legal: they produce a consistent point-in-time snapshot (anchor and
/// accumulated total move together).
///
/// A failed default-clock sample never commits a transition and never fabricates
/// a reading: state and counters stay truthful, `is_running()` and frozen
/// readings make the failure observable. An injected source self-falls-back
/// internally and exercises no failure branch on x86-64.
///
/// @note Spec `diagnostics/stopwatch`, requirement "Stopwatch state machine with
///       accumulated intervals".
/// @note Spec `diagnostics/stopwatch`, requirement "Zero-gap restart atomicity".
/// @note Spec `diagnostics/stopwatch`, requirement "O(1) lap and split
///       tracking".
/// @note Spec `diagnostics/stopwatch`, requirement "Hot path performs zero
///       allocation and meets latency budgets".
/// @note Diagnostic rendering follows project mandates 9 and 11 and is cold path
///       only; control, lap and query calls never format.
class alignas(64) Stopwatch {
public:
    /// @brief No-lap sentinel reported by `min_lap_ns()` before any lap.
    ///
    /// All ones distinguishes "no lap recorded" from a (physically impossible)
    /// zero-duration minimum.
    static constexpr std::uint64_t no_lap_value{0xFFFF'FFFF'FFFF'FFFFULL};

    /// @brief Construct in `Idle` on the default clock domain.
    ///
    /// Every stamp is `Timestamp::now()` (`CLOCK_MONOTONIC_RAW`); no time is
    /// accumulated and no lap exists yet.
    Stopwatch() noexcept = default;

    /// @brief Construct in `Idle` on a calibrated source domain.
    ///
    /// @param source Borrowed sampler; it is NOT owned and must outlive this
    ///        stopwatch. A null pointer selects the default clock domain.
    /// @note Spec `diagnostics/stopwatch`, requirement "Sampled from the nexigen
    ///       chrono clock domain".
    explicit Stopwatch(chrono::StampSource* source) noexcept : source_{source} {}

    /// @brief The injected stamp source.
    ///
    /// @return The borrowed source, or `nullptr` for the default clock domain.
    [[nodiscard]] constexpr chrono::StampSource* source() const noexcept { return source_; }

    // -- State control (design D4; spec "Stopwatch state machine ...")

    /// @brief Start or resume the timer.
    ///
    /// From `Idle` this begins a fresh interval; from `Stopped` it resumes while
    /// keeping the accumulated total; while `Running` it is a no-op. A first lap
    /// of the run measures from this same stamp. The transition commits only on
    /// a good sample.
    ///
    /// @note Spec `diagnostics/stopwatch`, requirement "Stopwatch state machine
    ///       with accumulated intervals".
    /// @note `noexcept`, allocation-free, lock-free and atomic-free.
    void start() noexcept {
        if (state_ == StopwatchState::Running) {
            return;
        }
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) {
            return;
        } // uncommitted (design D3)
        anchor_ = chrono::Timestamp{stamp};
        lap_anchor_ = anchor_; // first lap of the run measures from this stamp
        state_ = StopwatchState::Running;
    }

    /// @brief Stop: close the active interval into the accumulated total.
    ///
    /// Freezes the reading and leaves the timer `Stopped`; a no-op when not
    /// already `Running`. Lap statistics are untouched — the tail after the last
    /// `lap()` is deliberately not a lap. Commits only on a good sample.
    ///
    /// @note Spec `diagnostics/stopwatch`, requirement "Stopwatch state machine
    ///       with accumulated intervals".
    void stop() noexcept {
        if (state_ != StopwatchState::Running) {
            return;
        }
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) {
            return;
        } // uncommitted (design D3)
        accumulated_ns_ += stamp - anchor_.nanoseconds();
        state_ = StopwatchState::Stopped;
    }

    /// @brief Reset to `Idle`, discarding accumulated time and lap statistics.
    ///
    /// Works from any state and never samples; `min_lap_ns()` returns to the
    /// `no_lap_value` sentinel and `lap_count()` to zero.
    ///
    /// @note Spec `diagnostics/stopwatch`, requirement "Stopwatch state machine
    ///       with accumulated intervals".
    void reset() noexcept {
        accumulated_ns_ = 0U;
        last_lap_ns_ = 0U;
        min_lap_ns_ = no_lap_value;
        max_lap_ns_ = 0U;
        lap_count_ = 0U;
        state_ = StopwatchState::Idle;
    }

    /// @brief Zero-gap restart: close the current interval and open a fresh one.
    ///
    /// While `Running`, one stamp serves both the close and the fresh open, so no
    /// wall time passes uncounted between intervals. From `Stopped` the frozen
    /// total is returned and zeroed, then a fresh interval opens at a new sample;
    /// from `Idle` the return is zero. Lap statistics are kept and the timer
    /// always ends `Running`. On a failed sample nothing is mutated.
    ///
    /// @return Total elapsed nanoseconds at the switch (`0` from `Idle`); the
    ///         Running case returns the reading at the restart stamp.
    /// @note Spec `diagnostics/stopwatch`, requirement "Zero-gap restart
    ///       atomicity".
    [[nodiscard]] std::uint64_t restart() noexcept {
        if (state_ == StopwatchState::Running) {
            std::uint64_t stamp = 0U;
            if (!sample_ok_(stamp)) {
                return accumulated_ns_;
            } // uncommitted
            const std::uint64_t total = accumulated_ns_ + (stamp - anchor_.nanoseconds());
            accumulated_ns_ = 0U;
            anchor_ = chrono::Timestamp{stamp}; // same stamp as the close
            lap_anchor_ = anchor_;
            return total;
        }
        const std::uint64_t total = state_ == StopwatchState::Stopped ? accumulated_ns_ : 0U;
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) {
            return total;
        } // uncommitted
        accumulated_ns_ = 0U;
        anchor_ = chrono::Timestamp{stamp};
        lap_anchor_ = anchor_;
        state_ = StopwatchState::Running;
        return total;
    }

    // -- Lap / split timing (spec "O(1) lap and split tracking")

    /// @brief Record a split without stopping the timer.
    ///
    /// Returns nanoseconds since the previous `lap()` (or since `start()` for the
    /// first lap of the run) and re-anchors the lap segment at its own stamp; the
    /// run anchor is untouched, so `elapsed_ns()` keeps measuring the whole run
    /// as the sum of stage laps plus the live tail. While not `Running` this is a
    /// no-op returning zero: no phantom lap is ever recorded.
    ///
    /// @return The completed lap duration in nanoseconds; `0` when not running or
    ///         when the sample fails.
    /// @note Spec `diagnostics/stopwatch`, requirement "O(1) lap and split
    ///       tracking".
    [[nodiscard]] std::uint64_t lap() noexcept {
        if (state_ != StopwatchState::Running) {
            return 0U;
        }
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) {
            return 0U;
        } // uncommitted, no phantom lap
        const std::uint64_t duration = stamp - lap_anchor_.nanoseconds();
        lap_anchor_ = chrono::Timestamp{stamp};
        last_lap_ns_ = duration;
        if (duration < min_lap_ns_) {
            min_lap_ns_ = duration;
        } // sentinel-aware
        if (duration > max_lap_ns_) {
            max_lap_ns_ = duration;
        }
        ++lap_count_;
        return duration;
    }

    /// @brief Number of laps recorded since the last reset.
    ///
    /// @return Lap count; `0` before any lap.
    /// @note Spec `diagnostics/stopwatch`, requirement "O(1) lap and split
    ///       tracking". The `uint32_t` counter is unreachable at realistic lap
    ///       rates, and the increment is well-defined.
    [[nodiscard]] constexpr std::uint32_t lap_count() const noexcept { return lap_count_; }

    /// @brief Duration of the most recent lap.
    ///
    /// @return Lap duration in nanoseconds; `0` before any lap.
    [[nodiscard]] constexpr std::uint64_t last_lap_ns() const noexcept { return last_lap_ns_; }

    /// @brief Shortest lap recorded so far.
    ///
    /// @return Minimum lap duration in nanoseconds, or `no_lap_value` before any
    ///         lap, so an empty set is distinguishable from a zero minimum.
    [[nodiscard]] constexpr std::uint64_t min_lap_ns() const noexcept { return min_lap_ns_; }

    /// @brief Longest lap recorded so far.
    ///
    /// @return Maximum lap duration in nanoseconds; `0` before any lap.
    [[nodiscard]] constexpr std::uint64_t max_lap_ns() const noexcept { return max_lap_ns_; }

    /// @brief Snapshot the latest lap context without taking a lap.
    ///
    /// A pure read: `total_ns` is the elapsed at the instant of the call,
    /// `lap_number`/`lap_ns` report the latest lap, and `state` reports the
    /// lifecycle state. Nothing about control state or lap statistics mutates.
    ///
    /// @return A `LapSnapshot` value copyable in O(1).
    /// @note Spec `diagnostics/stopwatch`, scenario "Snapshotting does not
    ///       mutate the stopwatch".
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

    /// @brief Exact elapsed nanoseconds; never negative, never wrapped.
    ///
    /// In `Running` this takes one stamp; in `Stopped` it is pure arithmetic; in
    /// `Idle` it reports zero. On a default-clock failure the running reading
    /// freezes at the accumulated total.
    ///
    /// @return Total elapsed nanoseconds.
    /// @note Spec `diagnostics/stopwatch`, requirement "Multi-unit elapsed
    ///       queries do not mutate state".
    [[nodiscard]] std::uint64_t elapsed_ns() const noexcept {
        if (state_ == StopwatchState::Idle) {
            return 0U;
        }
        if (state_ == StopwatchState::Stopped) {
            return accumulated_ns_;
        }
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) {
            return accumulated_ns_;
        } // frozen read
        return accumulated_ns_ + (stamp - anchor_.nanoseconds());
    }

    /// @brief Whole microseconds, truncated from the nanosecond total.
    ///
    /// @return Elapsed whole microseconds as an integer (truncating, not
    ///         rounding).
    /// @note Spec `diagnostics/stopwatch`, requirement "Multi-unit elapsed
    ///       queries do not mutate state".
    [[nodiscard]] std::uint64_t elapsed_us_count() const noexcept { return elapsed_ns() / 1'000U; }

    /// @brief Fractional microseconds as a double.
    ///
    /// @return Elapsed microseconds derived from the same nanosecond total.
    /// @note Spec `diagnostics/stopwatch`, requirement "Multi-unit elapsed
    ///       queries do not mutate state".
    [[nodiscard]] double elapsed_us() const noexcept {
        return static_cast<double>(elapsed_ns()) / 1'000.0;
    }

    /// @brief Fractional milliseconds as a double.
    ///
    /// @return Elapsed milliseconds derived from the same nanosecond total.
    /// @note Spec `diagnostics/stopwatch`, requirement "Multi-unit elapsed
    ///       queries do not mutate state".
    [[nodiscard]] double elapsed_ms() const noexcept {
        return static_cast<double>(elapsed_ns()) / 1'000'000.0;
    }

    /// @brief Fractional seconds as a double.
    ///
    /// @return Elapsed seconds derived from the same nanosecond total.
    /// @note Spec `diagnostics/stopwatch`, requirement "Multi-unit elapsed
    ///       queries do not mutate state".
    [[nodiscard]] double elapsed_seconds() const noexcept {
        return static_cast<double>(elapsed_ns()) / 1'000'000'000.0;
    }

    // -- State readback

    /// @brief The current lifecycle state.
    ///
    /// @return `Idle`, `Running` or `Stopped`.
    [[nodiscard]] constexpr StopwatchState state() const noexcept { return state_; }

    /// @brief Whether the timer currently has an open interval.
    ///
    /// @return `true` exactly in the `Running` state.
    [[nodiscard]] constexpr bool is_running() const noexcept {
        return state_ == StopwatchState::Running;
    }

    /// @brief Render the live state as a cold-path diagnostic string.
    ///
    /// Reports the state name (riding the `StopwatchState` vocabulary), the total
    /// elapsed and the lap counter. Defined in
    /// src/diagnostics/stopwatch_format.cpp and never called from a hot path.
    ///
    /// @return Diagnostic text; identical to the free `to_string` helper.
    /// @note Spec `diagnostics/stopwatch`, requirement "Stopwatch diagnostic
    ///       rendering"; diagnostic only (project mandates 9 and 11).
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Result-free sampler face over the configured clock.
    ///
    /// @param stamp_ns_out Destination for the raw stamp on success.
    /// @return `true` with `stamp_ns_out` assigned on a good sample; `false` on a
    ///         default-clock failure. An injected source self-falls-back and
    ///         never fails.
    [[nodiscard]] bool sample_ok_(std::uint64_t& stamp_ns_out) const noexcept {
        if (source_ != nullptr) {
            stamp_ns_out = source_->sample().nanoseconds();
            return true;
        }
        const core::Result<chrono::Timestamp> stamp = chrono::Timestamp::now();
        if (!stamp.has_value()) {
            return false;
        } // branch, never .value()
        stamp_ns_out = stamp->nanoseconds();
        return true;
    }

    /// @brief Field layout pin: anchors and counters first, small tail last.
    ///
    /// 8-byte fields come first and the `uint32`/`uint8` tail last: 61 bytes of
    /// state, `sizeof == 64` under `alignas(64)`. Two anchors: `anchor_` is where
    /// the current run began (start/restart) and feeds the elapsed/stop/restart
    /// arithmetic; `lap_anchor_` is where the current lap segment began
    /// (start/restart/lap) and feeds split durations only, so elapsed keeps
    /// counting the whole run across laps.
    ///
    /// @note Spec `diagnostics/stopwatch`, scenario "Layout stays within one
    ///       cache line".
    chrono::Timestamp anchor_{};             ///< Open-interval run anchor.
    chrono::Timestamp lap_anchor_{};         ///< Open-lap segment anchor.
    std::uint64_t accumulated_ns_{};         ///< Sum of closed intervals.
    std::uint64_t last_lap_ns_{};            ///< Most recent lap duration.
    std::uint64_t min_lap_ns_{no_lap_value}; ///< Shortest lap (sentinel = none).
    std::uint64_t max_lap_ns_{};             ///< Longest lap.
    chrono::StampSource* source_{};          ///< Borrowed sampler; null = default.
    std::uint32_t lap_count_{};              ///< Laps since the last reset.
    StopwatchState state_{};                 ///< The lifecycle state (Idle).
};

/// @brief Free diagnostic helper mirroring `Stopwatch::to_string()`.
///
/// @param stopwatch Timer to render.
/// @return The same text the member returns.
/// @note Project mandate 9; cold path only (allocates a `std::string`).
[[nodiscard]] inline std::string to_string(const Stopwatch& stopwatch) {
    return stopwatch.to_string();
}

static_assert(sizeof(Stopwatch) == 64, "Stopwatch must own exactly one cache line");
static_assert(alignof(Stopwatch) == 64, "Stopwatch must be cache-line aligned");
static_assert(std::is_trivially_copyable_v<Stopwatch>,
              "Stopwatch must be a trivially copyable value type");
static_assert(sizeof(LapSnapshot) == 24, "LapSnapshot must stay a register-sized value bundle");
static_assert(std::is_trivially_copyable_v<LapSnapshot>, "LapSnapshot must be trivially copyable");
// Default member initializer pin: a fresh stopwatch sits in the no-lap state.
static_assert(([]() {
                  constexpr Stopwatch fresh{};
                  return fresh.min_lap_ns() == Stopwatch::no_lap_value && fresh.lap_count() == 0U;
              })(),
              "a fresh stopwatch must report the no-lap sentinel");
} // namespace nexigen::diagnostics

/// @brief `std::formatter` rendering a stopwatch as its diagnostic text.
///
/// Fill, width and alignment defer to the `std::string_view` formatter; the text
/// comes from the cold-path `to_string()`, so formatting never runs inside a
/// control, lap or query call.
///
/// @note Spec `diagnostics/stopwatch`, requirement "Stopwatch diagnostic
///       rendering"; project mandate 11.
template <>
struct std::formatter<nexigen::diagnostics::Stopwatch> : std::formatter<std::string_view> {
    /// @brief Format the stopwatch through its rendered diagnostic text.
    ///
    /// @tparam Ctx Format context supplied by `std::format`.
    /// @param stopwatch Timer to render.
    /// @param ctx Format context receiving the rendered text.
    /// @return The formatter's output iterator.
    template <class Ctx>
    auto format(const nexigen::diagnostics::Stopwatch& stopwatch, Ctx& ctx) const {
        const std::string text = stopwatch.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
