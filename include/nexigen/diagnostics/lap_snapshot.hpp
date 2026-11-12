// nexigen::diagnostics — snapshot of a stopwatch's current state (spec
// diagnostics/stopwatch "LapSnapshot captures a lap measurement").
#pragma once

#include <cstdint>
#include <string>

#include <nexigen/diagnostics/stopwatch_state.hpp>

namespace nexigen::diagnostics {

/// @brief Snapshot of a stopwatch's lifecycle state and latest lap context.
///
/// Returned by `Stopwatch::lap_snapshot()`: a plain, trivially copyable value
/// bundle that copies reported values only and never mutates the stopwatch.
/// `state` is first so the struct stays 24 bytes; copying is O(1). Snapshots of
/// a running stopwatch may differ in `total_ns` as wall time advances.
///
/// @note Spec `diagnostics/stopwatch`, requirement "LapSnapshot captures a lap
///       measurement".
/// @note Rendering follows project mandate 9 and is cold path only.
struct LapSnapshot {
    StopwatchState state;     ///< The lifecycle state observed at the snapshot.
    std::uint32_t lap_number; ///< 1-based index of the most recent lap (0 = none).
    std::uint64_t lap_ns;     ///< Duration of that most recent lap (0 = none).
    std::uint64_t total_ns;   ///< Elapsed nanoseconds at the snapshot.

    /// @brief Render the snapshot as a cold-path diagnostic string.
    ///
    /// @return Text naming the state, lap number, lap duration and total.
    /// @note Spec `diagnostics/stopwatch-state`, requirement "Diagnostics types
    ///       render cold-path diagnostics"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;

    /// @brief Compare every field of two snapshots.
    ///
    /// @return `true` when state, lap number, lap duration and total match.
    [[nodiscard]] friend constexpr bool operator==(const LapSnapshot&,
                                                   const LapSnapshot&) = default;
};

/// @brief Free diagnostic helper mirroring `LapSnapshot::to_string()`.
///
/// @param snapshot Snapshot to render.
/// @return The same text the member returns.
/// @note Project mandate 9; cold path only (allocates a `std::string`).
[[nodiscard]] inline std::string to_string(const LapSnapshot& snapshot) {
    return snapshot.to_string();
}

} // namespace nexigen::diagnostics
