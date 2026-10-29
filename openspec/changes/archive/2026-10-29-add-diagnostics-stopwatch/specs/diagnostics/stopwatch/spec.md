# Spec Delta

## Purpose

Defines the core high-precision interval timer for real-time systems: the state machine (`Idle`/`Running`/`Stopped`) with resumed-accumulation control, the zero-gap `restart()`, O(1) lap/split statistics, non-mutating multi-unit elapsed queries, the `LapSnapshot` value type, clock-source selection between `Timestamp::now()` and a calibrated `Stamp_source`, and the zero-allocation hot-path guarantees with latency gates.

## ADDED Requirements

### Requirement: Stopwatch state machine with accumulated intervals

A `Stopwatch` SHALL model the lifecycle states `StopwatchState::Idle`, `Running`, `Stopped`: `start()` from `Idle` begins a fresh timing interval; `stop()` freezes the boundary and accumulates the active interval into the total; `start()` from `Stopped` resumes while preserving the accumulated total; `reset()` zeroes accumulated time, clears lap statistics, and returns to `Idle` from any state. All control operations SHALL be `noexcept` and modify only the stopwatch's own inline storage.

#### Scenario: Fresh stopwatch is idle with zero elapsed

- **WHEN** a `Stopwatch` is default-constructed and queried before any control call
- **THEN** its state is `Idle`, `is_running()` is false, and every elapsed query reports zero

#### Scenario: Start running transitions from idle

- **WHEN** time advances after `start()` on an idle stopwatch
- **THEN** the state becomes `Running`, `is_running()` is true, and observed elapsed time grows with the work performed

#### Scenario: Stop freezes while preserving the reading

- **WHEN** `stop()` is called after a measured interval and elapsed queries are repeated without intervening work
- **THEN** the state becomes `Stopped`, `is_running()` is false, and repeated elapsed queries return the same frozen value (no sampling advances it)

#### Scenario: Resume accumulates across stop/start cycles

- **WHEN** a stopwatch runs an interval, stops, starts again, runs a second interval, and stops
- **THEN** the two intervals are accumulated: the final reading equals the sum of both measured intervals within the sampling tolerance

#### Scenario: Reset clears all state

- **WHEN** `reset()` is called from `Running` or `Stopped` with accumulated time and completed laps
- **THEN** the state returns to `Idle`, all elapsed queries report zero, and lap counts and lap statistics return to their no-lap defaults

### Requirement: Zero-gap restart atomicity

`restart()` SHALL atomically return the total accumulated elapsed nanoseconds (including the active interval closed at the sample taken for the restart), clear that total, and begin a fresh interval anchored at that exact same timestamp sample — no wall time passes uncounted between the old interval's close and the new one's open. The state remains `Running`, and the transition takes no clock sample beyond the restart sample itself.

#### Scenario: Reported total includes the closing interval

- **WHEN** `restart()` is called on a running stopwatch covering a measurable interval
- **THEN** the returned value matches the running elapsed reading at the moment of the call (within sampling tolerance) and is monotonically consistent across repeated cycles

#### Scenario: Restart anchors the next interval at the same sample

- **WHEN** `restart()` is followed immediately by work and a later elapsed query
- **THEN** the query reports only time since the restart — no pre-restart wall time leaks into the fresh interval, and a second stopwatch spanning the same wall time reads the sum of the (pre-restart + post-restart) readings with no measurable gap in excess of the sampling tolerance

#### Scenario: Restart from stopped clears and resumes

- **WHEN** `restart()` is invoked while the stopwatch is `Stopped` with an accumulated (frozen) total
- **THEN** the frozen total is returned and reported, the stopwatch re-enters `Running`, and the fresh interval runs independent of the previously accumulated total

### Requirement: O(1) lap and split tracking

`lap()` SHALL return nanoseconds elapsed since the previous `lap()` (or `start()` for the first lap) without stopping the stopwatch, updating the lap anchor to its sample. The stopwatch SHALL track in O(1) inline storage `lap_count()`, `last_lap_ns()`, `min_lap_ns()`, and `max_lap_ns()`; before any lap, `last_lap_ns()` and `max_lap_ns()` SHALL be 0 and `min_lap_ns()` SHALL report a distinct no-lap sentinel.

#### Scenario: First lap measures from the segment start

- **WHEN** `lap()` is called once on a newly started running stopwatch after measurable work
- **THEN** the returned lap time covers the work since `start()`, and no `stop()` state change occurs (`is_running()` remains true)

#### Scenario: Consecutive laps measure only their own segment

- **WHEN** `lap()` is called after two separate work segments — the second lap is longer than the first
- **THEN** the second value reflects only the second segment's duration, `lap_count()` grows by one per lap, and min/max statistics bracket the recorded lap times

#### Scenario: No-lap defaults are queryable

- **WHEN** lap statistics are queried before any lap has been recorded
- **THEN** `lap_count()` is zero, `last_lap_ns()` and `max_lap_ns()` are zero, and `min_lap_ns()` reports the no-lap sentinel rather than zero (so a minimum is distinguishable from an empty set)

#### Scenario: Calling lap while stopped is a no-op

- **WHEN** `lap()` is invoked while the stopwatch is not `Running`
- **THEN** it returns zero, does not advance the lap anchor, and touches no counter (no phantom lap is recorded)

### Requirement: LapSnapshot captures a lap measurement

The module SHALL provide a trivially copyable `LapSnapshot` value struct: `lap_number` (one-based index of the most recent lap), `lap_ns` (its duration), `total_ns` (elapsed at the snapshot), and `state` (the `StopwatchState` observed). A `Stopwatch` SHALL expose a non-mutating query materializing the snapshot without taking a lap; the struct is a value companion to the scalar lap counters, copyable in O(1).

#### Scenario: Snapshot reflects the latest lap

- **WHEN** a `LapSnapshot` is taken after two recorded laps on a running stopwatch
- **THEN** `lap_number` is 2, `lap_ns` equals the second lap's duration (matching the last lap reading), `total_ns` matches the current elapsed total (within sampling tolerance), and the state reports `Running`

#### Scenario: Snapshot with no laps reports the empty state

- **WHEN** a `LapSnapshot` is taken before any lap has been recorded
- **THEN** `lap_number` is zero and `lap_ns` is zero while `total_ns` still reports the current accumulation

#### Scenario: Snapshotting does not mutate the stopwatch

- **WHEN** snapshots are taken repeatedly (in `Idle`, `Running`, and `Stopped`) and then further work is measured
- **THEN** each snapshot copies only reported values, and the traced values are unchanged by snapshotting itself except for the natural growth of a running measurement

### Requirement: Multi-unit elapsed queries do not mutate state

`elapsed_ns()` (`std::uint64_t`), `elapsed_us()` (double), an integer whole-microsecond query (`std::uint64_t`), `elapsed_ms()` (double), and `elapsed_seconds()` (double) SHALL be `noexcept`, `[[nodiscard]]`, valid in both `Running` and `Stopped`, and mutate nothing. Derived units SHALL come from the same nanosecond total — double forms from the raw count, the integer microsecond form truncating whole microseconds — and stay mutually consistent at one instant.

#### Scenario: Queries are valid in running and stopped states

- **WHEN** elapsed queries in every unit are taken both mid-run and after a stop
- **THEN** each returns a plausible value for the work performed (nonzero and bounded by the performed work, within a generous tolerance), and neither call changes state, `is_running()`, the accumulated total, or the lap statistics

#### Scenario: Unit conversions scale correctly against the nanosecond total

- **WHEN** elapsed is queried after a controlled busy-wait of a known duration
- **THEN** the microsecond and millisecond readings correspond to the nanosecond reading's magnitude (e.g., µs within [ns/1000 × (1 ± 1 µs tolerance)]) and the seconds reading spans the same duration scaled, with the integer microsecond form truncating rather than rounding

#### Scenario: Elapsed remains non-decreasing while running

- **WHEN** elapsed nanoseconds are sampled in a loop (`Running`) with no control operations
- **THEN** successive samples never decrease, and reaching a `Stopped` state freezes the reading

### Requirement: Sampled from the nexigen chrono clock domain

A `Stopwatch` SHALL take its samples from `nexigen::chrono`: by default `Timestamp::now()` (`CLOCK_MONOTONIC_RAW`), or from a caller-owned `chrono::Stamp_source*` passed at construction for the calibrated-TSC sampling path (~3–5 ns per sample). A null source selects the default clock; the source is borrowed, not owned, and must outlive the stopwatch's use. Elapsed SHALL never report negative or silently wrapped values.

#### Scenario: Default construction uses the raw monotonic clock

- **WHEN** a stopwatch is started and stopped around a busy-wait interval, then repeated
- **THEN** elapsed readings built on the default source are strictly positive across multiple loops and consistent with CLOCK_MONOTONIC_RAW semantics: intervals never decrease when measured serially

#### Scenario: Explicit Stamp_source stamps drive the same semantics

- **WHEN** a stopwatch is constructed with a calibrated `Stamp_source*` pointer and the same lifecycle exercised around busy-wait segments
- **THEN** elapsed is consistently positive and non-decreasing, and the lifecycle-restart accounting matches the default source's semantics (the two sources differ only in stamp cost, not in interval accounting)

#### Scenario: Null source pointer falls back to the default clock

- **WHEN** a stopwatch is constructed with an explicit `nullptr` source and used normally
- **THEN** behavior matches the default-constructed stopwatch

### Requirement: Hot path performs zero allocation and meets latency budgets

Every state transition (`start()`, `stop()`, `reset()`, `restart()`), lap call, and elapsed query SHALL be `noexcept` with zero heap allocation, zero locks, zero atomics, and no `std::string`/JSON work. Release SHALL gate warm medians: `start()` + `stop()` ≤ 100 ns default-clock, ≤ 20 ns calibrated; elapsed queries and `lap()` ≤ 20 ns calibrated; gates compile away outside Release. The object SHALL be a stack-resident trivially copyable value of at most one cache line.

#### Scenario: Hot operations never allocate

- **WHEN** allocation-counting hooks are armed and a loop drives full lifecycle operations — start/stop cycles, laps, restarts, elapsed queries
- **THEN** zero heap allocations and zero locks occur across the entire loop

#### Scenario: Release latency gates hold for control operations

- **WHEN** the Release build's median harness measures start+stop, restart, lap, and elapsed-query operations in tight warm loops on both clock sources
- **THEN** the medians stay within their budgets (≤ 100 ns default-clock start+stop; ≤ 20 ns calibrated-source start+stop, restart, lap, and elapsed query) and the medians are above zero (a genuine measurement, not an optimized-out call)

#### Scenario: Layout stays within one cache line

- **WHEN** the static layout assertions compile the stopwatch type
- **THEN** its size fits a single 64-byte cache line and remains trivially copyable, so a member stopwatch never splits hot state across lines

### Requirement: Stopwatch diagnostic rendering

`Stopwatch` SHALL provide the cold-path member `std::string to_string() const` and ADL free `to_string(const Stopwatch&)` that render the live state: the state name (riding the `StopwatchState` diagnostic vocabulary), the total elapsed in a human-readable form, and the lap counter. Formatting at width via a `std::formatter` specialization SHALL defer to the rendered string.

#### Scenario: Rendering reports live state

- **WHEN** `to_string()` and `std::format("{}", sw)` are evaluated on a running stopwatch with accumulated time and recorded laps
- **THEN** both render the state name, the total elapsed, and the lap count — identical to the free-function rendering of the same object

#### Scenario: Rendering is cold-path only

- **WHEN** any stopwatch hot-path operation list is driven repeatedly with allocation counting armed
- **THEN** each `to_string()` call is permitted to allocate, but no hot-path operation does; the diagnostic string does not participate in any state reading used by the control path
