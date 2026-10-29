# Proposal

## Why

The `nexigen::diagnostics` and `nexigen::metrics` modules are each demonstrated in isolation, but no runnable example shows the intended composition: a stopwatch measuring a scope and feeding the result straight into a histogram. Integrators adopting both modules currently have to infer the wiring (clock calibration, guard sink, provider/meter factory, snapshot rendering) from separate examples and headers. A prototype of this pipeline already runs and self-verifies; this change brings it under the spec-driven contract and makes it part of the documented example catalogue.

## What Changes

- Add one self-contained executable, `ex_stopwatch_histogram`, under `examples/diagnostics/`: a calibrated `Stamp_source` clocks light/heavy `ScopedStopwatch` probes, geometric bucket bounds are derived from that envelope, every measured tick records its elapsed nanoseconds into an `ExplicitBucketHistogram` through the guard's callable sink, and a whole-run `Stopwatch` covers the measurement phase.
- Render the histogram on the console: count/sum/min/max, `p50`/`p90`/`p99`/`p99.9`, a per-bucket table with ASCII bars, and the cold-path `HistogramSnapshot::to_string()` diagnostic.
- Self-verify the pipeline (one sample per tick, bucket counts summing to the total, configured bounds as bucket edges, ordered percentiles, recorded durations inside the whole-run interval) and exit non-zero on any mismatch.
- Extend the shared `nexigen_add_example(name source target ...)` helper to accept extra link targets through `${ARGN}` so a cross-module example can link both `nexigen::diagnostics` and `nexigen::metrics` without duplicating the CTest registration logic.
- Register the executable through the existing helper (CTest smoke test, `examples` label, 600 s timeout) and document its command and behavior in the README `## 🧪 Runnable Examples` section.
- Correct the README's introductory standard claim from C++20 to C++23 so the documented language level matches the build (`CMAKE_CXX_STANDARD 23`) and the rest of the README.
- Grow the documented catalogue from eleven to twelve executables.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `examples`: the "Runnable example catalogue" and "Documented example invocation" requirements change from eleven to twelve executables, adding `ex_stopwatch_histogram` under `examples/diagnostics/`.
- `diagnostics/examples`: adds a requirement describing the `ex_stopwatch_histogram` demonstration (stopwatch-to-histogram recording, console rendering, self-verification, CTest registration) alongside the existing `ex_stopwatch` requirements.

## Impact

- **New source**: `examples/diagnostics/ex_stopwatch_histogram.cpp`.
- **Build**: `examples/CMakeLists.txt` helper gains an optional extra-target parameter; `examples/diagnostics/CMakeLists.txt` registers `ex_stopwatch_histogram` against `nexigen::diagnostics` and `nexigen::metrics`. One new CTest entry (label `examples`, timeout 600) in every configuration.
- **Docs**: `README.md` Runnable Examples command list and example catalogue; introductory standard claim corrected to C++23.
- **API**: no library headers, types, or signatures change.
- **Performance / latency / allocations**: zero hot-path impact. The example is cold-path consumer code: recording calls the existing allocation-free, wait-free `ExplicitBucketHistogram::record()` from a single thread, and the provider runs with `collect_interval = 0` (no background harvester). Clock calibration and all histogram/provider storage are allocated at startup; printing, `std::string`, and `%f` formatting happen outside the measurement loop. The example measures with the calibrated `Stamp_source` (`~3–5 ns` per sample) but asserts no latency thresholds and changes no `*_STRICT_LATENCY` gate.
- **Dependencies**: none new; the example links `nexigen::diagnostics` and `nexigen::metrics` (both bring `core`/`chrono` transitively) plus `Threads::Threads` via the shared helper.
- **Concurrency**: single-threaded measurement domain, no new atomics, shards, or memory-order choices; no `alignas(64)` layout changes.
