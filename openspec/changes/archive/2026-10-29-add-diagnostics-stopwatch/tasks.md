# Tasks

## 1. Module scaffold and build target

- [x] 1.1 Create the directory structure `include/nexigen/diagnostics/`, `src/diagnostics/`, `tests/diagnostics/`, `examples/diagnostics/`; verify all four exist and match the namespace/directory mapping (design D1).
- [x] 1.2 Add the `nexigen_diagnostics` STATIC target to the root `CMakeLists.txt` after `nexigen::tracing` (alias `nexigen::diagnostics`, `file(GLOB ... src/diagnostics/*.cpp CONFIGURE_DEPENDS)` with the tracing-style zero-source placeholder guard, PUBLIC `nexigen_core` + `nexigen_chrono`, no Threads/_GNU_SOURCE); verify `cmake -S . -B build && cmake --build build --target nexigen_diagnostics` configures and builds the empty module.

## 2. StopwatchState enum and serialization contract

- [x] 2.1 Write `include/nexigen/diagnostics/stopwatch_state.hpp` per design D5: `enum class StopwatchState : std::uint8_t` (`Idle`, `Running`, `Stopped`), constexpr `to_string` (PascalCase spellings, `"Unknown"` default arm), constexpr `from_string` (out-param untouched on failure), ADL `to_json`/non-throwing `from_json` under `JSON_NOEXCEPTION=1`, `std::formatter<StopwatchState>` in `namespace std` (templated context), pinned by static_asserts; verify it compiles standalone under the project's flags.
- [x] 2.2 Add the test entry to `tests/CMakeLists.txt` (`nexigen_diagnostics_tests` on `tests/diagnostics/diagnostics_test.cpp`, linking `nexigen::diagnostics`, `TIMEOUT 600`) and cover the enum contract in it: enumerator round-trips, `from_string` rejection without mutation, garbage-cast `"Unknown"` rendering, JSON round-trip + non-string/malformed rejection without throwing, `std::format("{}", state)` and fill/align; verify with `ctest -R nexigen_diagnostics_tests`.

## 3. Core Stopwatch and LapSnapshot

- [x] 3.1 Write `include/nexigen/diagnostics/stopwatch.hpp` per design D2–D4: `alignas(64)` layout with `static_assert(sizeof == 64 / alignof == 64 / trivially copyable)`, sampling helper (`Stamp_source*` injection, null = default clock, no second monotonic clamp), `start()`/`stop()`/`reset()` state machine with idempotent transitions, zero-gap `restart() -> uint64_t`, O(1) lap tracking (`lap()`, `lap_count()`, `last_lap_ns()`, `min_lap_ns()` sentinel, `max_lap_ns()`), `LapSnapshot` (24 bytes, state-first) with `lap_snapshot()`, elapsed queries (`elapsed_ns`, `elapsed_us` double, integer `elapsed_us_count`, `elapsed_ms`, `elapsed_seconds`), `state()`/`is_running()` — all hot ops `noexcept` + `[[nodiscard]]` where returning; verify it compiles under `-fno-exceptions`.
- [x] 3.2 Add `src/diagnostics/stopwatch_format.cpp` with cold-path member definitions of `Stopwatch::to_string()` and `LapSnapshot::to_string()` plus the inline free `to_string(const T&)` helpers declared in the headers (design D1/D6); verify the target picks the unit up via the glob and links.
- [x] 3.3 Extend `tests/diagnostics/diagnostics_test.cpp` with the stopwatch suite per its spec: full state transitions (`Idle`→`Running`→`Stopped`→`Running` resume accumulation), reset clears everything, `restart()` totals + zero-gap continuity against a second spanning stopwatch, lap tracking (first-lap/consecutive segment accounting, min/max/count, no-lap sentinel, no-op while stopped), `LapSnapshot` (after laps, empty state, non-mutating), elapsed queries in both states (plausibility windows, unit conversions, non-decreasing reads, non-mutating), both clock sources + null-fallback, hot-loop zero-allocation with the armed counter, `to_string`/`std::format` member-free parity; add the `NEXIGEN_DIAGNOSTICS_STRICT_LATENCY` Release/RelWithDebInfo gate definition to `tests/CMakeLists.txt` with the median harness (design D10) asserting `start()+stop()` ≤ 100 ns default / ≤ 20 ns calibrated and `restart()`/`lap()`/elapsed ≤ 20 ns calibrated, printing the overhead-benchmark medians; verify Debug and Release `ctest -R nexigen_diagnostics_tests` pass.

## 4. ScopedStopwatch RAII guard

- [x] 4.1 Write `include/nexigen/diagnostics/scoped_stopwatch.hpp` per design D7–D9: class template with CTAD, the six-constructor overload matrix (`Stamp_source*`? × {`uint64_t&`, sink} × budget?), integral-binding disambiguation static_asserts, non-copyable/non-movable, exactly-once destruction delivery (out-ref write or non-throwing sink invoke), mid-scope `has_exceeded_budget(budget_ns)` (strictly greater) + no-argument stored-default overload, inline member + free `to_string`, partial `std::formatter<ScopedStopwatch<Sink>>` specialization, no `from_json`; verify it compiles under `-fno-exceptions`.
- [x] 4.2 Extend `tests/diagnostics/diagnostics_test.cpp` with the guard suite per its spec: out-ref destination receives elapsed ≥ performed work, sink invoked exactly once at destruction, copy/move rejected by compile, mid-scope budget checks (exceeded true / within false / stored default vs. unconfigured), source + destination + budget constructor combinations, build-mode behavior in Debug/Release, hot-loop allocation-free guard lifecycles, construct+destruct ≤ 20 ns calibrated gate, `to_string`/`std::format` rendering; verify Debug and Release `ctest -R nexigen_diagnostics_tests` pass.

## 5. Runnable example and README

- [x] 5.1 Create `examples/diagnostics/CMakeLists.txt` (via `nexigen_add_example(ex_stopwatch ex_stopwatch.cpp nexigen::diagnostics)`) and wire `add_subdirectory(diagnostics)` into `examples/CMakeLists.txt`; write `examples/diagnostics/ex_stopwatch.cpp` demonstrating per-tick `ScopedStopwatch` budget checks with an overrun summary, multi-stage `lap()` profiling with min/max/count/total, a `restart()` round, and an out-param guard; verify the executable runs standalone with exit 0 and `ctest -L examples` passes.
- [x] 5.2 Update `README.md`: add the `nexigen::diagnostics` API section (linking the target, Stopwatch/ScopedStopwatch quickstart) and name `ex_stopwatch` in the Runnable Examples catalogue (eleven executables); verify the documented configure/build/run commands from the clean-checkout reader's perspective.

## 6. Full verification matrix

- [x] 6.1 Configure and build in Debug, run `ctest` — 100% of unit tests and examples pass; verify no new warnings under the project's `-Werror` configuration.
- [x] 6.2 Configure and build in Release (`-O3 -march=native -flto` via project defaults), run `ctest` — 100% pass with the `NEXIGEN_DIAGNOSTICS_STRICT_LATENCY` gates active; verify the printed `start()` + `stop()` overhead-benchmark medians are within their budgets.
- [x] 6.3 Configure with `-DNEXIGEN_ENABLE_TSAN=ON`, build, run `ctest` — 100% pass with no ThreadSanitizer reports.
- [x] 6.4 Configure with `-DNEXIGEN_ENABLE_ASAN=ON`, build, run `ctest` — 100% pass with no AddressSanitizer/UBSan reports.
- [x] 6.5 Run `openspec validate add-diagnostics-stopwatch` and `openspec status` — the change validates clean and all tasks reflect the delivered state.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied (`/opsx-archive`).
- Verify the archived result.
