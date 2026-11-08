// nexigen::diagnostics — snapshot of a stopwatch's current state (spec
// diagnostics/stopwatch "LapSnapshot captures a lap measurement").
#pragma once

#include <cstdint>
#include <string>

#include <nexigen/diagnostics/stopwatch_state.hpp>

namespace nexigen::diagnostics {

struct LapSnapshot {
    StopwatchState state;     ///< The lifecycle state observed at the snapshot.
    std::uint32_t lap_number; ///< 1-based index of the most recent lap (0 = none).
    std::uint64_t lap_ns;     ///< Duration of that most recent lap (0 = none).
    std::uint64_t total_ns;   ///< Elapsed nanoseconds at the snapshot.

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

} // namespace nexigen::diagnostics
