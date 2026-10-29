# Design

## Context

See `proposal.md` — Why. The modules exist and are stable: `nexigen::diagnostics` provides `Stopwatch`/`ScopedStopwatch` over `nexigen::chrono::Stamp_source`, and `nexigen::metrics` provides `MetricProvider` → `Meter` → `ExplicitBucketHistogram` with allocation-free `record()` and cold-path `read()`. The shared `nexigen_add_example(name source target)` helper links one target plus `Threads::Threads` and registers a CTest smoke test with the `examples` label and 600 s timeout. `examples/diagnostics/` currently holds only `ex_stopwatch`; the catalogue spec enumerates eleven executables. The prototype pipeline already exists in the working tree and passed Debug, Release, TSan, and ASan.

## Goals / Non-Goals

**Goals:**
- One focused example that composes the two modules end to end and stays deterministic across build modes.
- A minimal, backward-compatible extension of the shared CMake helper for cross-module examples.
- Coherent spec and README updates (eleven → twelve executables).

**Non-Goals:**
- No new library API, instrument, clock, sink, or header change.
- No multi-threaded measurement, background harvesting, or affinity demonstration — `ex_histograms` and `ex_metric_provider_and_sinks` already cover those concerns.
- No latency assertions or benchmark gates; the example is functional documentation, not a benchmark.

## Decisions

### D1 — Location and link targets

Place the executable in `examples/diagnostics/` because the stopwatch drives the demonstration; link both `nexigen::diagnostics` and `nexigen::metrics`. The metrics target already brings `core`/`chrono` transitively, and `diagnostics` brings `core`/`chrono`, so the link line stays additive with no new dependency. *Alternative considered:* `examples/metrics/` — rejected because the primary subject is scope timing, and the metrics catalogue spec enumerates exactly four executables under that directory.

### D2 — Extend the helper with optional extra targets

Change `nexigen_add_example(name source target)` to forward `${ARGN}` into `target_link_libraries`: `PRIVATE ${target} ${ARGN} Threads::Threads`. Existing calls pass no extra arguments, so `${ARGN}` expands to nothing and their link lines are unchanged; the new call passes `nexigen::diagnostics nexigen::metrics`. This keeps one CTest registration path and avoids duplicating the helper's timeout/label logic. *Alternative considered:* an inline `add_executable`/`target_link_libraries`/`add_test` block in `examples/diagnostics/CMakeLists.txt` — rejected as a second, divergent registration path.

### D3 — Calibrated clock and probe-derived bounds

One `Stamp_source::calibrate()` serves the whole example; all probes and ticks call the inline `sample()` path (no syscall). Two `ScopedStopwatch` probes (200 and 3,000 work units) establish a light/heavy envelope; eight geometric bounds span `[light/2, heavy*1.5]` with a guaranteed minimum spread of 4× and a `+1.0` nudge that enforces strict monotonicity. This keeps the histogram populated in Debug, Release, and sanitizer builds where absolute spin rates differ by an order of magnitude. *Alternative considered:* fixed nanosecond bounds — rejected because sanitizer builds would collapse every sample into the overflow bucket, making the report uninformative.

### D4 — Measurement and recording path

The whole measurement phase runs on the main thread: a calibrated `Stopwatch` brackets the loop, and each tick body sits inside `ScopedStopwatch{&stamps, sink}` whose callable sink invokes `histogram->record(static_cast<double>(elapsed_ns))` at destruction. `record()` is `noexcept`, allocation-free, and wait-free; `ScopedStopwatch` is stack-only and stores the sink inline. The provider is configured with `collect_interval = 0` (no background harvester) and a `NullMetricSink`, so the only cross-thread machinery is whatever the histogram itself contains — no new atomics, shards, memory orders, or `alignas(64)` layout are introduced. The histogram is `SingleThreaded`/`Readable`/`Cumulative`, matching the single-writer contract. *Alternative considered:* `MultiThreaded` producers — rejected as unnecessary concurrency for a single-threaded timing demo and a TSan surface with no pedagogical value here.

### D5 — Console rendering and self-verification

The report is emitted with `std::printf` (cold path): summary line, percentile line, one row per bucket with an ASCII bar scaled to the peak bucket, then `snapshot.to_string()`. Bars are built in a fixed `char[41]` buffer — no allocation per row. The example asserts its invariants before returning: count equals tick count, `ignored_samples == 0`, bucket counts sum to the total, `bucket_count == bounds + 1`, snapshot edges equal the configured bounds, `min <= max`, ordered percentiles, populated `p50`, and total recorded durations not exceeding the whole-run `Stopwatch`. Any failure prints on stderr and yields exit status 1, matching the catalogue's exit-status requirement.

### D6 — Documentation

Add the run command and a catalogue bullet to the README `## 🧪 Runnable Examples` section, and correct the introductory claim from C++20 to C++23 so the README, CMake (`CMAKE_CXX_STANDARD 23`), and the module API sections agree. No other README section changes.

## Risks / Trade-offs

- [Probe timings vary by build mode / host load] → bounds are derived from the probes at run time, and the example asserts structural invariants (counts, ordering, containment) rather than latency thresholds.
- [A tick can land in the overflow bucket] → the overflow bucket is a valid, counted bucket; the self-check sums all buckets plus the total, so this never fails spuriously.
- [Strict `-Wconversion`/`-Wsign-conversion` flags] → all narrowing points use explicit casts (`unsigned long long` for `printf`, `static_cast<unsigned>` for bar length, `static_cast<double>` for durations); verified with `-Werror`.
- [`${ARGN}` could disturb existing examples] → it is empty for every current call; the four build modes were re-run after the helper change.
- [Duplication with `ex_stopwatch` and `ex_histograms`] → intentional: this example is the only place the two modules meet, and each existing example keeps its narrower focus.

## Migration Plan

Additive only: add the source and CMake registration, extend the helper, update the README/spec catalogue. Rollback is deleting `examples/diagnostics/ex_stopwatch_histogram.cpp`, removing its registration line, and reverting the `${ARGN}` forwarding and README edits; no library behavior or persisted data is affected.

## Open Questions

None.
