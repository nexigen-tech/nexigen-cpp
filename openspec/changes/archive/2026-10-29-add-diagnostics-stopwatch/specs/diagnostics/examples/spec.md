# Spec Delta

## Purpose

Defines the runnable `ex_stopwatch` example for the `nexigen::diagnostics` module: a self-contained demonstration of real-time loop budget checking, multi-stage lap profiling, and RAII scope measurement.

## ADDED Requirements

### Requirement: ex_stopwatch demonstrates loop tick budgeting

The example SHALL profile a simulated real-time loop: each simulated tick body executes staged work bounded by a budget, guarded by a `ScopedStopwatch` way of reporting, and the example SHALL report per-tick elapsed time and budget status on the console — demonstrating how a loop guard detects overruns with `has_exceeded_budget` and prints a summary of ticks over budget across the run.

#### Scenario: Tick budget run prints per-tick timing

- **WHEN** the example runs to completion
- **THEN** the console lists each simulated tick's elapsed time against the configured budget, differing/staged stage work is visible, and ticks that exceeded the budget are labeled as such in the summary

### Requirement: ex_stopwatch demonstrates multi-stage lap profiling

The example SHALL demonstrate stage-level profiling with `Stopwatch`: one instrumented loop with a lap at every stage boundary, each stage's duration printed with `lap_count()`, `last_lap_ns()`, min/max lap endpoints, and the total. It SHALL also demonstrate `restart()` (a measured interval closing into a fresh one, total re-accumulating from zero) and the `ScopedStopwatch` out-argument destination writing a scope's elapsed into a `std::uint64_t&` the caller prints afterwards.

#### Scenario: Stage laps are reported per stage

- **WHEN** the example's profiling section completes
- **THEN** each stage's lap duration is printed with the running count, min/max lap statistics are reported, and the total elapsed matches the sum of stage laps (rounding into tolerance)

#### Scenario: Restart replaces a measured interval cleanly

- **WHEN** the example calls `restart()` between profiling rounds
- **THEN** the printed returned total reflects the elapsed before restart, and the following round's readings start fresh from (near) zero

### Requirement: ex_stopwatch is buildable, runnable, and a CTest smoke test

`ex_stopwatch` SHALL be a self-contained executable under `examples/diagnostics/`, registered through the shared `nexigen_add_example` helper against the `nexigen::diagnostics` target, exposed as a CTest entry with the `examples` label and the shared timeout, exiting 0 on success and non-zero on failure, and passing under the project's Debug, Release, ThreadSanitizer, and AddressSanitizer configurations.

#### Scenario: Example runs to exit zero

- **WHEN** `ex_stopwatch` is built and launched directly (or via CTest) without arguments
- **THEN** it completes its demonstrations, exits with status 0, and emits no sanitizer diagnostics in any build mode

#### Scenario: Example seeds its own data and needs no harness

- **WHEN** the example is run outside any test runner
- **THEN** it creates its own loops, budgets, and timing data, requires no fixture or pre-existing files, and prints its report to stdout
