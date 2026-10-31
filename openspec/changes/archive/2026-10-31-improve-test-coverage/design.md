# Design

## Context

See `proposal.md` — Why and the coverage budget. The baseline was produced by the `coverage` target (`68df35a`) on GCC 15.2, Debug, seven unit-test binaries. Uncovered lines were extracted from `gcov -j` JSON and grouped by function:

- `logging_settings.cpp`: `sanitize_document` (57+12 lines), `apply_environment` (32), `parse_u32`/`parse_affinity` (8).
- `app_settings.cpp`: `apply_tracing_environment` (17), four `to_json` overloads (16+11+8+4), `load_*` error paths (15), `apply_metrics_environment` (8).
- JSON writers: escape-byte tables (15), integer/double/char/string append branches (~20).
- Sinks: `JsonFileMetricSink::on_batch` (34) and `flush`/`write_all` (5); `ConsoleMetricSink::on_batch` (31).
- `app.hpp`: failing-app template branches (~50 across logger/metrics/tracer failure paths and watcher run), plus stop/shutdown branches.

## Goals / Non-Goals

**Goals:**

- Overall line coverage **≥96%**, every module **≥90%**, per-file targets from the proposal.
- Cover real behaviour through public APIs; no production test hooks.
- Keep all tests deterministic and within the existing zero-dependency harnesses.
- Prove the result with the `coverage` target and the four build modes.

**Non-Goals:**

- Production code changes (if a bug is found, stop and open a separate fix change).
- Branch coverage as a gate (line coverage is the contract).
- Covering unreachable defensive `default:` arms or platform-specific paths that cannot execute on the target.
- Replacing examples-as-smoke-tests with coverage sources (the target stays unit-test-only).

## Decisions

### D1 — Extend existing suites; no new executables

All cases go into the existing module TUs (`logging_test.cpp`, `app_test.cpp`, `metrics_test.cpp`, `tracing_test.cpp`, `chrono_test.cpp`). CMake, `coverage` target and harness conventions stay unchanged. Table-driven helpers keep each case short.

### D2 — Settings loaders: valid + invalid per key

For `logging_settings.cpp`:
- valid values for every `LoggerOptions` field and each nested `spam_protection` field;
- wrong type for each key (string/number/bool swapped), asserting one diagnostic and the previous/default value;
- `parse_affinity` tokens: empty, leading/trailing comma, `1-`, `-2`, `3-1`, `64`, `0-63`, duplicate ids, spaces;
- env overrides for all eight variables, including overflow (`18446744073709551616`), invalid enums, invalid affinity, `0` capacities;
- global/local file read failure via a directory path; malformed global and local separately;
- `to_json`/`to_string` assertions for every config field.

For `app_settings.cpp`:
- all metrics keys (`collect_interval_ns`, `core_mask`, all six capacities) and all tracing keys (`core_mask`, `max_tracers`, `max_spans_per_window`, `window_ns`);
- invalid types, negative/overflowing numbers, `max_tracers` 0 and 65;
- all six environment variables, valid and invalid;
- global/local unreadable paths, malformed documents, aggregate worst-status cases;
- `to_json`/`to_string` for `Metrics_settings`, `Tracing_settings`, `App_settings_paths`, `App_settings`.

### D3 — JSON writers: drive escaping through sinks

Use the public sink APIs with payloads containing every escaped class: `"`, `\`, `\n`, `\r`, `\t`, `\b`, `\f`, bytes `0x01..0x1f`, DEL, UTF-8 Cyrillic, and a 120-byte string. Include empty names/values, INT64_MIN/MAX, doubles (`0`, `-0.0`, min/max, inf/nan rejection where the writer rejects), and a value that exactly fills the scratch buffer (overflow path). Assert the emitted line parses as JSON where applicable and the writer reports the expected `Status`.

### D4 — Sinks: success and failure paths

- `JsonFileMetricSink`: batches containing every `MetricKind`; unopenable path (directory); write failure via a closed/read-only descriptor; `flush()` after a failed write.
- `ConsoleMetricSink`: write to a pipe (capture both the happy path and a closed pipe error).
- `JsonFileTraceSink`: start/flush/stop, rotation prefix (`RotationPolicy::Hourly/Daily`), oversized record, open failure.
- `ConsoleTraceSink`: normal line and closed-descriptor error.
- `ChromeTraceSink`: empty buffer, first-record comma handling, flush.

### D5 — App branches

- Failing logger, metrics-sink and tracer hooks individually; assert `initialize` fails, `current()` is null, and `shutdown()` is safe.
- First stop reason wins in both orders (`Requested` then `Sigint`, and reverse).
- Worker-thread signal-mask probe via `pthread_sigmask` in a helper thread after initialization.
- Shutdown reason at `Error`/`Fatal` thresholds: assert current behaviour (record present or filtered) so the contract is pinned; if filtering is a real defect, stop and open a fix change.
- Two sequential App instances: `current()` points at the initialized one and is cleared by shutdown; `meter()`/`tracer_for()` before init return `NotInitialized`.

### D6 — Remaining modules

- `metrics::snapshot_format`: `to_string`/`to_json` for every snapshot/config type and drop-stat combinations.
- `spam_protection`: repeat-window boundary, token-bucket refill, bypass floor, reconfigure.
- `chrono::Stamp_source`: calibration success and repeated `calibrate()`/sample monotonic clamp paths reachable on this platform.
- `metrics/provider.cpp`, `tracing/record_format.cpp`, `logging/rotation.cpp`, `file_logger.hpp`: remaining reachable error/format branches through existing public fixtures. Unreachable branches are recorded, not forced.

### D7 — Measurement loop

After each task group:

```bash
cmake --build build/coverage --target coverage
```

If a file is below target, extract its remaining uncovered lines from `gcov -j` JSON and add the next concrete case. Do not lower a target and do not add production hooks. At the end, run all four build modes.

### D8 — Documented unreachable exceptions

A handful of residual lines cannot be reached through public APIs on this target: the `std::to_chars` error arms in the metrics/tracing JSON writers (the local digit buffers always fit), the non-x86 `Stamp_source` fast-path definition, and several `open`/`read`/`fstat` failure arms that require fault injection at the syscall boundary. These are recorded as exceptions rather than forced with `#ifdef`-only tests or production hooks; the achieved overall number is still ≥95%.

## Risks / Trade-offs

- **Coverage overfitting** → assertions check values/status/JSON shape, not line numbers.
- **Runtime growth** → keep loops small; the metrics suite is already the long pole.
- **Signal timing** → reuse the 100 ms margins and process-wide `kill`; no new ad-hoc timing.
- **Unreachable lines** → documented; if they prevent 95%, stop and propose a production change rather than faking tests.
- **A test exposes a real defect** → stop this change and open a bug fix; do not silently change production here.

## Migration Plan

Test-only and additive. Rollback: revert the added tests and documentation; no production or on-disk changes.

## Open Questions

None blocking. The `App` shutdown-reason threshold behaviour is pinned as-is by tests; a defect fix would be a separate change.
