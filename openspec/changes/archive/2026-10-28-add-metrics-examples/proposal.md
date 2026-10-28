# Proposal

## Why

`nexigen::metrics` ships a full instrument/sink/provider API but no runnable examples: `examples/metrics/` does not exist, the README names only the four core/logging executables, and nothing demonstrates the intended end-to-end usage of the factory hierarchy, the access/reset policies, or the sink pipeline. Two API-level defects currently make the module unbuildable or un-composable at all (a `thread_slot()` call-site arity break, and `CompositeMetricSink` rejecting the config-constructed `JsonFileMetricSink`), so a self-contained, runnable demonstration cannot exist until they are fixed. Concrete examples are also the cheapest executable documentation for HIL integrators adopting the metrics API.

## What Changes

- Fix the `detail::thread_slot()` call-site arity break at 6 sites (`counter.hpp:84`, `gauge.hpp:81,85`, `descriptive_statistics.hpp:146`, `explicit_histogram.hpp:180`, `exponential_histogram.hpp:237`) so `nexigen_metrics` and its consumers compile again; the fix passes each instrument's `MetricKind` to the per-kind slot registry as `thread_slot.hpp:9-13` documents.
- Extend `CompositeMetricSink<Sinks...>` with member-injection construction so a composite containing the config-constructed, non-default-constructible `JsonFileMetricSink` is constructible (today `composite_metric_sink.hpp:33-34` makes `CompositeMetricSink<ConsoleMetricSink, JsonFileMetricSink, NullMetricSink>` impossible).
- Fix the `ResetOnRead` swap handshake in `DescriptiveStatistics` (`take_slot`) so records racing a window swap land exactly once: read/clear a bank only under a stable even `seq`, and defer the untouched bank to the next window on handshake timeout (scope addition approved during implementation — the examples' exactly-once window checks cannot pass otherwise).
- Fix the exponential-histogram coarsening loss (`Exponential_bank::coarsen()`) so bucket collapse never drops samples: out-of-window buckets fold into edge buckets (scope addition approved during implementation — the examples' "totals account for every accepted sample" check cannot pass otherwise).
- Fix instrument registration vs. harvest traversal (`Meter::create_*` publishes slots under `harvest_mutex_`) so instrument creation during background collection is race-free (scope addition approved during implementation — the TSan matrix gate cannot pass otherwise).
- Add four self-contained example executables under `examples/metrics/`, registered via the established `nexigen_add_example` helper (CTest smoke test, `examples` label): `ex_counters_and_gauges`, `ex_descriptive_statistics`, `ex_histograms`, `ex_metric_provider_and_sinks`.
- Wire `add_subdirectory(metrics)` into `examples/CMakeLists.txt`; there is no `examples/metrics/.gitkeep` placeholder to remove — the directory is created fresh.
- **Supersede** the example decomposition planned by the in-flight `add-metrics-subsystem` change: its `ex_counters_and_stats` / `ex_histograms_and_provider` pair and its tasks 13.1–13.3 are replaced by the four executables above; this change's `examples` delta is authoritative for the catalogue.
- Document the four metrics examples in the README `## 🧪 Runnable Examples` section with build/run instructions; the catalogue grows from four executables to eight.
- Verify 100% build and CTest pass across Debug, Release, ThreadSanitizer, and AddressSanitizer.

## Capabilities

### New Capabilities

- `metrics/examples`: the runnable metrics example catalogue at behavior level — what each of the four executables demonstrates (policies, windows, histogram families, provider/sink pipeline) and the verification scenarios each must satisfy.

### Modified Capabilities

- `examples`: the "Runnable example catalogue" requirement changes from six named executables to eight (the four existing core/logging ones plus `ex_counters_and_gauges`, `ex_descriptive_statistics`, `ex_histograms`, `ex_metric_provider_and_sinks`), and "Documented example invocation" changes to name all eight. This delta supersedes the six-name catalogue text staged by `add-metrics-subsystem`.

## Impact

- **Headers**: `include/nexigen/metrics/detail/thread_slot.hpp` call sites in `counter.hpp`, `gauge.hpp`, `descriptive_statistics.hpp`, `explicit_histogram.hpp`, `exponential_histogram.hpp`; `include/nexigen/metrics/composite_metric_sink.hpp` gains a member-injection constructor; `include/nexigen/metrics/descriptive_statistics.hpp` `take_slot` gains the stable-even-`seq` read/clear and timeout deferral; `include/nexigen/metrics/metric_provider.hpp` `Meter::create_*` bodies serialize slot publication with `harvest_mutex_`.
- **New sources**: `examples/metrics/ex_counters_and_gauges.cpp`, `ex_descriptive_statistics.cpp`, `ex_histograms.cpp`, `ex_metric_provider_and_sinks.cpp`, plus `examples/metrics/CMakeLists.txt`.
- **Build**: `examples/CMakeLists.txt` adds `add_subdirectory(metrics)`; four new CTest entries (label `examples`, timeout 600) in every configuration.
- **Docs**: `README.md` Runnable Examples section and a `nexigen::metrics` API note.
- **Supersession**: `add-metrics-subsystem` tasks 13.1–13.3 and its `examples` delta become obsolete and should be retired from that change (e.g. via `/opsx-update`) so the two changes cannot land contradictory catalogue text. Its tasks 4.x–12.x are untouched by this change.
- **Performance / latency / allocations**: zero hot-path impact. All fixes are cold-path, call-site-only, or consumer-side (the `thread_slot()` fix adds no work beyond the intended per-kind `thread_local` lookup; the composite constructor is construction-time; the swap-handshake fix leaves the producer hot path and all memory orders unchanged and keeps the consumer spin bounded). The examples are cold-path consumers: recording loops use the existing wait-free instrument paths unchanged, while printing, JSON parsing, and file I/O happen outside any real-time loop. No new heap allocation on library hot paths; example startup may allocate freely (examples are not real-time code). No latency-gate values change.
- **Dependencies**: none new; examples link `nexigen::metrics` (+ `Threads::Threads` via the helper) and reach `nexigen_options` transitively through `nexigen_core`.
