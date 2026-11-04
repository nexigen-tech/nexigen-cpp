# Proposal

## Why

The 2026-11-03 analysis left seven P2 defects (build/output/API drift/
composite asymmetry/lifecycle hardening) and two copy-paste code smells
(P3). They are mostly small, independent correctness and observability
fixes; in one case they keep real runtime losses invisible (silent
drop of over-size sink batches), which justifies doing them now ahead of
the hub dashboard work.

## What Changes

- **build/** — LTO stops being a no-op: the Release pipeline sets the
  `INTERPROCEDURAL_OPTIMIZATION_<CONFIG>` property instead of marking the
  INTERFACE `nexigen_options` target (P2-16).
- **output/** — masked diagnostics render `core_mask` as real hexadecimal
  (`0x…` + decimal correctly, via `std::format("{:#x}")` in chrono, metrics
  and tracing `to_string`s) (P2-17).
- **sinks/** — JSON-line sinks observe their own scratch-overflow losses:
  `JsonFileMetricSink` counts discarded batches and `JsonFileTraceSink`
  counts discarded span lines; both counters show in `to_string()` (P2-18;
  spec deltas in `metrics/metric-sinks` + `tracing/trace-sinks`).
- **logging/** — `LoggerOptions` stops pretending to configure byte quotas
  that no code path reads: `repeat_suppression_max_count`,
  `max_bytes_per_sec_per_shard`, `max_total_bytes_per_sec` are removed from
  the settings contract **BREAKING** (config struct, to_json/from_json,
  to_string, settings parser and the logger-options spec defaults updated;
  P2-19). The engine-geometry fields
  (`max_producer_threads`, `spsc_ring_buffer_capacity`, `message_capacity`)
  stay — verified against the tree, they ARE connected through
  `logging::check_capacity` bounds checks in `FileLogger`/`ConsoleLogger`
  initialize (the analysis's "unwired" claim was wrong for them) — and
  `flush_cpu_affinity`, which is wired through the runner logger, is now
  printed by `to_string` too.
- **metrics/** — `Gauge<MultiThreaded>::set` lands the target value
  deterministically with a shard-0 relink (no lost check-then-act delta);
  `StateGauge` docs stop claiming wait-free readers (seqlock retry),
  `read()` documents its thread_local buffer lifetime (P2-20).
- **logging/tracing** — `CompositeLogger::flush` returns the first member
  error, matching the `CompositeTraceSink` contract (error contract
  symmetry, P2-22).
- **app/** — `SignalWatcher::start` guards double-start with `Conflict`
  (the `app/application` spec already requires it); the ×4 copy-paste
  `fail`-idioma in `Infrastructure::initialize` folds into one helper;
  the runner's pre-exec handshake writes ignore SIGPIPE like every other
  IPC writer (P2-23).
- **core/** — the three per-module JSON `FixedWriter` copies collapse into
  one `<nexigen/core/detail/fixed_writer.hpp>` (P3-24).
- **core/** — `read_settings_file`/`parse_u64`/`add_diagnostic` collapse
  from three copies into `<nexigen/core/detail/settings_io.hpp>` (P3-25).
- **repo/** — `.gitignore` gains `build-*/` so sanitizer/coverage build
  trees stay untracked by default (P3-26).

## Capabilities

### New Capabilities

(none — no new system behavior; writers/settings helpers are internal)

### Modified Capabilities

- `metrics/metric-sinks`: the JSON-lines file sink requirement gains the
  overflow-observability clause.
- `tracing/trace-sinks`: the JSONL output requirement gains the
  overflow-observability clause.
- `logging/logger-options`: `SpamProtectionConfig` drops the three dead
  quota fields from the defaults scenario.

## Impact

- Code: `cmake/Optimizations.cmake`; three `to_string`s
  (`chrono_provider.cpp`, `snapshot_format.cpp`, `record_format.cpp`);
  `json_file_metric_sink.hpp` + the tracing sink write path (counters +
  to_string); `logger_options.hpp` + `logger_options_format.cpp` +
  `logging_settings.cpp` + the spam config consumers (dead quota fields);
  `gauge.hpp` + `state_gauge.hpp` (semantics/docs); `composite_logger.hpp`;
  `app/infrastructure.hpp` (watcher guard + helper); `runner.cpp` +
  `app_suite.cpp` (SIGPIPE); new `include/nexigen/core/detail/
  fixed_writer.hpp` replacing the three per-module writers; new
  `include/nexigen/core/detail/settings_io.hpp` replacing the three helper
  copy groups; `.gitignore`.
- Tests: hex render checks, sink overflow counters (metrics + trace),
  gauge set under concurrent add (MT sanity), double-start watcher guard,
  logger-options render/round-trip updates, settings-loader contract
  tests.
- Performance/latency/allocation: zero hot-path impact — all changes are
  cold-path diagnostics, config parsing, or build properties; the gauge
  `set` keeps the control-plane single pass (shard stores, no CAS loops);
  no new allocations outside cold paths.
- Deferred: P3-28 (CRTP serialization-mandate generator) stays open —
  wide mechanical refactor with churn risk vs. no behavior gain; tracked
  separately after the dashboard lands.
