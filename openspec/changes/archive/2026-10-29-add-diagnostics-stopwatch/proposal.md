# Proposal

## Why

`nexigen` counts events (`metrics`), logs them (`logging`) and traces spans (`tracing`), but has no direct wall-clock measuring tool: a real-time loop that must swear "this stage took 12 µs, under my 40 µs budget" cannot ask any existing module, and ad-hoc `std::chrono` calls neither accumulate split times nor reuse the calibrated TSC domain. Real-time HIL platforms need an interval timer whose own `start()`/`stop()`/`lap()` overhead sits in the tens of nanoseconds — the same stamp quality `chrono::Stamp_source` already provides — never allocates, never locks, and fails to `std::string`/JSON only on cold diagnostic paths. Building the `nexigen::diagnostics` module now keeps this measuring vocabulary out of downstream ad-hoc code and reuses the proven `chrono` stamping and serialization contracts.

## What Changes

- Create the module build infrastructure: `include/nexigen/diagnostics/`, `src/diagnostics/`, `tests/diagnostics/`, `examples/diagnostics/`, the CMake target `nexigen_diagnostics` with alias `nexigen::diagnostics` (links `nexigen::core` + `nexigen::chrono` PUBLIC), test/example registration, and the README's `nexigen::diagnostics` documentation.
- Define the `nexigen::diagnostics` enum catalogue — `StopwatchState` (`Idle`, `Running`, `Stopped`) — with `constexpr` `to_string`/`from_string`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization, and extend the module-wide serialization contract to every public type: member + free cold-path `to_string()` on `Stopwatch`, `LapSnapshot`, and `ScopedStopwatch`.
- Add the core `Stopwatch` in `include/nexigen/diagnostics/stopwatch.hpp`: a cache-line-aligned, trivially copyable interval timer with zero heap allocations and `noexcept` hot paths, powered by `nexigen::chrono` (`Timestamp::now()` by default, optional non-owning `Stamp_source*` for ~3–5 ns calibrated-TSC sampling). State control `start()`/`stop()`/`reset()` with resumed-accumulation semantics, `restart()` returning total elapsed nanoseconds and re-anchoring the new interval at the exact same sample (zero gap between intervals), O(1) inline lap/split tracking (`lap()`, `lap_count()`, `last_lap_ns()`, `min_lap_ns()`, `max_lap_ns()`, plus a `LapSnapshot` snapshot struct), and non-mutating elapsed queries valid in both `Running` and `Stopped` states (`elapsed_ns()`, `elapsed_us()` double and integer-microsecond forms, `elapsed_ms()`, `elapsed_seconds()`, `state()`, `is_running()`).
- Add a `LapSnapshot` value struct capturing one lap measurement (`lap_number`, `lap_ns`, `total_ns`) with the cold-path serialization surface.
- Add the RAII `ScopedStopwatch` in `include/nexigen/diagnostics/scoped_stopwatch.hpp`: sample at construction, on destruction write elapsed nanoseconds to a caller-provided `uint64_t&` or invoke a non-throwing callable sink exactly once; mid-scope `has_exceeded_budget()` (stored default threshold and explicit-argument forms) for real-time loop overrun detection; optional `Stamp_source*` injection.
- Add `tests/diagnostics/diagnostics_test.cpp` (one TU, zero-dependency harness): full state-transition coverage (`Idle` → `Running` → `Stopped` → `Running` resume accumulation), zero-gap `restart()` continuity, lap tracking min/max/count, `ScopedStopwatch` out-param/sink/budget behavior, full `to_string()` / `nlohmann::json` / `std::formatter` coverage, allocation-freedom checks, and Release-only latency gates (`NEXIGEN_DIAGNOSTICS_STRICT_LATENCY`): `start()` + `stop()` ≤ 100 ns on the default clock and ≤ 20 ns on a calibrated `Stamp_source`. The Release configuration doubles as the overhead benchmark verifying `start()` + `stop()` cost.
- Add the runnable example `examples/diagnostics/ex_stopwatch.cpp` demonstrating per-tick budget checking with `ScopedStopwatch`, multi-stage `lap()` profiling, and guard-to-total accounting, registered as a CTest `examples`-label smoke test; the example catalogue grows from ten executables to eleven with README documentation for `ex_stopwatch`.

## Capabilities

### New Capabilities

- `diagnostics/stopwatch-state`: the `StopwatchState` enum and the module-wide `to_string`/`from_string`, `nlohmann::json`, and `std::format` serialization contract for every public `nexigen::diagnostics` type.
- `diagnostics/stopwatch`: the core interval-timer lifecycle — start/stop/reset/resume accumulation, zero-gap `restart()`, O(1) lap statistics, non-mutating elapsed queries in `ns`/`µs`/`ms`/`s`, `LapSnapshot`, clock-source selection (`Timestamp` vs calibrated `Stamp_source`), and the zero-allocation hot-path guarantees with latency gates.
- `diagnostics/scoped-stopwatch`: the RAII scope guard — out-param and non-throwing-sink destinations, exactly-once destruction reporting, budget-overrun detection, and the same hot-path guarantees.
- `diagnostics/examples`: the runnable `ex_stopwatch` example and its observable behavior.

### Modified Capabilities

- `examples`: the runnable example catalogue expands from ten executables to eleven, adding `ex_stopwatch` under `examples/diagnostics/`, wired via the existing `nexigen_add_example` helper, and the README invocation guide names all eleven.

## Impact

- **New public headers**: `include/nexigen/diagnostics/` — `stopwatch_state.hpp` (enum + serialization contract), `stopwatch.hpp` (`Stopwatch` + `LapSnapshot`), `scoped_stopwatch.hpp`. All sampling/transition hot paths stay inline in headers.
- **New sources**: `src/diagnostics/stopwatch_format.cpp` — the cold-path `to_string` rendering units (`Stopwatch`, `LapSnapshot`); the templated `ScopedStopwatch` surface stays header-only.
- **New build target**: `nexigen_diagnostics` (`nexigen::diagnostics`) linking `nexigen_core` and `nexigen_chrono` PUBLIC, registered after `nexigen::tracing` in the root `CMakeLists.txt`; no new third-party dependency.
- **New tests/examples**: `tests/diagnostics/diagnostics_test.cpp` registered as `nexigen_diagnostics_tests` with the Release-only latency gate; `examples/diagnostics/` added to `examples/CMakeLists.txt` and the README.
- **Performance**: every sampling transition (`start`, `stop`, `restart`, `lap`) plus all elapsed queries complete with at most one `Timestamp::now()` (~20–25 ns vDSO) or one `Stamp_source::sample()` (~3–5 ns calibrated TSC) plus a handful of register stores; `restart()` and `lap()` re-anchor at the sample they just took, so adjacent intervals share one timestamp (no measurable gap). Release gates: `start()` + `stop()` median ≤ 100 ns (default clock) and ≤ 20 ns (calibrated `Stamp_source`); guard construct + destruct ≤ 20 ns on the calibrated path; elapsed queries while running ≤ 20 ns. Non-x86 targets pay one `Timestamp::now()` per sample (platform budget, correctness first).
- **Memory allocations**: zero heap allocation on every hot path (state transitions, laps, elapsed queries, guard construction/destruction); the type is a single stack-resident 64-byte object; `std::string`/`nlohmann::json` remain strictly cold-path (diagnostics rendering only).
- **Dependencies**: none added; reuses `nlohmann::json` transitively via `nexigen_core` and the `nexigen::chrono` stamp surface. No `Threads::Threads` needed (no background worker, no affinity). Concurrency: the class carries no atomics — single-thread domain by design (fresh instance per thread), so no cross-core line contention; `alignas(64)` keeps it off neighbouring hot data when embedded in loop-local structures.
- **Compatibility**: purely additive; no existing public API, target, or spec requirement is removed. The `examples` catalogue text changes only to include the new executable.
