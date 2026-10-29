// nexigen::diagnostics — cold-path diagnostic rendering (design.md D1/D6;
// spec diagnostics/stopwatch "Stopwatch diagnostic rendering" and
// diagnostics/stopwatch-state "Diagnostics types render cold-path
// diagnostics").
//
// This is the compiled cold unit of the module: the to_string renderings of
// Stopwatch and LapSnapshot. They allocate a std::string, so they are
// diagnostics/config utilities and NEVER appear on a hot path (mandate 4).
// The hot state machines, laps, queries and the templated
// ScopedStopwatch surface all stay inline in the headers.
//
// Rendering shape mirrors the module neighbours: `Type(field1=..., ...)` with
// the enum name riding the mandate-11 constexpr to_string(StopwatchState).
#include <cstdint>
#include <string>

#include <nexigen/diagnostics/stopwatch.hpp>
#include <nexigen/diagnostics/stopwatch_state.hpp>

namespace nexigen::diagnostics {

/// The live state: state name (enum contract), total elapsed (ns, laid out as
/// seconds.nanoseconds like Timestamp's rendering for scan-past familiarity)
/// and the lap counter. Reads control state only — never samples the hot
/// path and never changes it.
[[nodiscard]] std::string Stopwatch::to_string() const {
    const std::uint64_t total_ns = elapsed_ns();
    std::string frac = std::to_string(total_ns % 1'000'000'000ULL);
    const std::size_t digits = frac.size();
    if (digits < 9U) {
        frac.insert(0, 9U - digits, '0');
    }
    return "Stopwatch(state=" + std::string(nexigen::diagnostics::to_string(state_)) +
           ", elapsed_ns=" + std::to_string(total_ns / 1'000'000'000ULL) + '.' + frac +
           ", laps=" + std::to_string(lap_count_) + ")";
}

/// The latest lap: its 1-based number, duration and the snapshot total.
[[nodiscard]] std::string LapSnapshot::to_string() const {
    return "LapSnapshot(number=" + std::to_string(lap_number) +
           ", lap_ns=" + std::to_string(lap_ns) +
           ", total_ns=" + std::to_string(total_ns) +
           ", state=" + std::string(nexigen::diagnostics::to_string(state)) + ")";
}

} // namespace nexigen::diagnostics
