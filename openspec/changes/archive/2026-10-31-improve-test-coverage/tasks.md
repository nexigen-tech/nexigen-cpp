# Tasks

## 1. Logging settings loader — +110 lines

- [x] 1.1 Add valid/invalid per-key cases for every `LoggerOptions` and `SpamProtectionConfig` field (wrong types, nested spam, missing/malformed/unreadable files, all eight env overrides including overflow/invalid values), plus `to_json`/`to_string` assertions for every field. Verify: `nexigen_logging_tests` passes and `src/logging/logging_settings.cpp` reaches **≥96%** (≥416/432) in the coverage report.
- [x] 1.2 Add the affinity grammar matrix (`""`, `,`, `1-`, `-2`, `3-1`, `64`, `0-63`, duplicates, spaces) and the global `app_name`-is-ignored rule. Verify: all `parse_affinity`/`plain_string` branches covered; the file target holds.

## 2. App settings loader — +85 lines

- [x] 2.1 Add valid/invalid cases for every metrics key and every tracing key, all six env overrides, `max_tracers` 0/65, interval overflow, global/local unreadable paths, malformed documents and aggregate worst-status combinations. Verify: `nexigen_app_tests` passes and `src/app/app_settings.cpp` reaches **≥94%** (≥321/339).
- [x] 2.2 Cover all four `to_json` overloads and every `to_string` field (including diagnostics arrays and loaded flags). Verify: JSON assertions cover every key; the file target holds.

## 3. JSON writers — +38 lines

- [x] 3.1 Drive `src/metrics/json_writer.cpp` through `ConsoleMetricSink`/`JsonFileMetricSink` with all escaped classes (`"` `\` `\n` `\r` `\t` `\b` `\f`, `0x01..0x1f`, DEL, UTF-8), empty/long values, and numeric boundaries. Verify: metrics writer reaches its reachable target (89.7%; the remaining `std::to_chars` error arms are documented unreachable in design D8) and emitted lines parse as JSON.
- [x] 3.2 Drive `src/tracing/json_writer.cpp` through the trace sinks with the same matrix plus hex ids and double formatting. Verify: tracing writer reaches its reachable target (90.9%; the remaining `std::to_chars`/hex overflow arms are documented unreachable in design D8).

## 4. Metric and trace sinks — +56 lines

- [x] 4.1 Cover `JsonFileMetricSink` (every `MetricKind` in a batch, unopenable path, write failure, flush after failure) and `ConsoleMetricSink` (pipe success and closed-pipe error). Verify: sinks reach **≥92%** (`json_file_metric_sink.hpp` ≥123/133; `console_metric_sink.hpp` ≥106/112).
- [x] 4.2 Cover `JsonFileTraceSink` start/flush/stop, rotation prefixes, oversized record and open failure; `ConsoleTraceSink` success/error; `ChromeTraceSink` empty buffer, comma handling and flush. Verify: trace sinks reach **≥92%**, `chrome_trace_sink.hpp` ≥95% (≥106/111).

## 5. App lifecycle branches — +10 `app.hpp` lines

- [x] 5.1 Add failing `emplace_logger`, failing metrics sink and failing tracer hooks; assert rollback, null `current()`, and safe `shutdown()`. Verify: all failure branches run; `include/nexigen/app/app.hpp` reaches **≥96%** (≥145/150).
- [x] 5.2 Add first-stop-wins in both orders, bool-tick false, worker signal-mask probe, shutdown reason at `Error`/`Fatal`, and two sequential App instances (`current()` publish/clear, `NotInitialized` factories). Verify: assertions pass and the `app.hpp` target holds.

## 6. Remaining module gaps — +~95 lines

- [x] 6.1 `metrics/snapshot_format.cpp`: cover every snapshot/config `to_string`/`to_json` and drop-stat combination → **≥94%** (≥334/352).
- [x] 6.2 `spam_protection.hpp` (window boundary, refill, bypass, reconfigure) → **≥93%** (≥43/46); `chrono/stamp_source.hpp` calibration/monotonic paths → reachable branches covered (remaining non-x86 fast-path arms documented in design D8); `metrics/provider.cpp` → **≥96%**; `tracing/record_format.cpp` → **≥97%**.
- [x] 6.3 `logging/rotation.cpp` → **≥95%** (≥113/119); `file_logger.hpp` → **≥97%** (≥181/185); `app_stop_reason.hpp` → 100% (24/24). Verify: each target holds or the remaining lines are documented as unreachable in design D8.

## 7. Coverage gate, docs and matrix

- [x] 7.1 Run the coverage target and confirm **overall ≥96%** and **every module ≥90%** (with the per-file targets above). If any target is missed, add the next concrete case from the gcov JSON; do not change production code. Verify: coverage output pasted into the change notes.
- [x] 7.2 Document the achieved numbers in `README.md` / `docs/architecture.md` and state `NEXIGEN_COVERAGE_FAIL_UNDER=95` as the project expectation (no CMake enforcement change). Verify: documented command reproduces the report.
- [x] 7.3 Run Debug, Release (`-O3 -march=native -flto`), TSan (`-DNEXIGEN_ENABLE_TSAN=ON`) and ASan (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify: `ctest --test-dir <dir> --output-on-failure` reports 100% in all four modes.
- [x] 7.4 Run `openspec validate improve-test-coverage --strict`. Verify: command reports valid.

## Workflow follow-up

- Archive the change after the ≥96% coverage report, docs, and four-mode matrix are complete.
