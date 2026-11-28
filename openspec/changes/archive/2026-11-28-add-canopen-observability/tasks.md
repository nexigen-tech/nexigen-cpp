# Tasks

## 1. Scaffolding

- [x] 1.1 Add the `nexigen_canopen_observability` static target (headers under
  `include/nexigen/canopen/observability/`, sources under `src/canopen/observability/`, links
  `nexigen_canopen`, `nexigen_logging`, `nexigen_metrics`), register it in the install/export
  lists, and verify a clean configure plus `cmake --build <build-dir> -j 64` succeeds with
  `nexigen_canopen` still telemetry-free.
- [x] 1.2 Add a `nexigen_canopen_observability_tests` CTest target with its own test source
  directory, and verify `ctest -R canopen_observability` discovers and runs the empty suite.

## 2. Public vocabulary

- [x] 2.1 Add `CanOpenRuntimeState` (`Stopped`, `Starting`, `Running`, `Degraded`, `Error`) with
  canonical `to_string`/`from_string`, `nlohmann::json`, and `std::formatter`; verify round-trip,
  malformed-JSON, and formatter tests.
- [x] 2.2 Add `CanOpenErrorDomain` (`Bus`, `Transport`, `Nmt`, `Sdo`, `Pdo`, `Emcy`, `Signal`,
  `Ipc`, `Config`) with the same serialization surface; verify round-trip, malformed-JSON, and
  formatter tests.
- [x] 2.3 Add `CanOpenLogPolicy` with the documented defaults, range validation,
  `max_cold_diagnostics`, `to_string`, `nlohmann::json`, and `std::formatter`; verify default
  values, malformed-field tolerance, range rejection, and formatter parity tests.
- [x] 2.4 Add full Doxygen comments to the new public headers and verify the Debug build compiles
  with the project warning flags.

## 3. Metrics registration

- [x] 3.1 Add `detail::RuntimeMetrics` with one pointer per Tier 1 instrument and a
  `register_metrics(metrics::Meter&)` factory; verify every instrument is `MultiThreaded`,
  `Readable`, and `Cumulative`.
- [x] 3.2 Implement the per-bus meter identity helper (`canopen.<bus>`, version from config) and
  reject empty, over-long, or duplicate bus names; verify `canopen.can0` success plus
  `InvalidArgument` cases.
- [x] 3.3 Verify registration creates exactly 25 counters, 4 gauges, 1 state gauge, and 1 explicit
  histogram with the documented names, units, and kinds via a `collect_now()` snapshot.
- [x] 3.4 Verify a deliberately small `MetricProviderConfig` returns `ResourceExhausted` for the
  first instrument that does not fit and that no transport is opened on failure.

## 4. Error gate

- [x] 4.1 Add `detail::EpisodeGate` with the documented 40-byte layout and the
  `std::array<std::array<EpisodeGate, 9>, 128>` storage; verify the `static_assert` and the
  absence of atomics or dynamic allocation.
- [x] 4.2 Implement `on_error` actions for first occurrence, repeated code, changed code, and
  suppression; verify each action with unit tests and with an allocation trap armed.
- [x] 4.3 Implement `poll_recovery` with `recovery_hold_ms` and verify recovery suppression,
  flapping, and the suppressed-count report in unit tests.
- [x] 4.4 Implement the global repeat budget and the per-domain summary; verify first-error and
  recovery records bypass the budget, repeats respect it, and an idle interval emits no summary.
- [x] 4.5 Add a test proving that error, repeat, recovery, and summary handling never allocate and
  never take a lock.

## 5. Cold-path helpers

- [x] 5.1 Add a cold-path EDS result logger that reports path, hash, object/PDO counts, and the
  first `max_cold_diagnostics` diagnostics with an omitted-count trailer; verify success,
  overflow, and zero-diagnostic cases with a fake logger.
- [x] 5.2 Add lifecycle and NMT-transition log helpers with the documented `Info` level; verify
  the emitted text carries bus, node, and transition context.
- [x] 5.3 Verify that no core `nexigen_canopen` translation unit includes or links
  `nexigen_logging`/`nexigen_metrics` after the new target is added.

## 6. Documentation

- [x] 6.1 Write `docs/observability-embedding.md` with the `OBS-BOOT`, `OBS-COLD`, `OBS-EDGE`,
  `OBS-HOT-ERR`, `OBS-HOT-MET`, `OBS-SUM`, `OBS-SNAP`, and `OBS-SPAN` scenarios, their rules, the
  deferred CANopen span/PHM correlation points, and references to `synchro`, `com`, and CANopen
  examples; verify every named scenario has a definition and a code reference.
- [x] 6.2 Write `docs/adr/0005-observability-embedding.md` recording runtime-owned telemetry,
  logger/meter injection, no-exception error handling, and the no-duplication rule; verify it
  links to the embedding document and the CANopen spec.
- [x] 6.3 Update `GLOSSARY.md` and `GLOSSARY_RUS.md` with the scenario names, "error episode",
  "windowed summary", and "observability embedding" terms; verify both glossaries stay aligned.
- [x] 6.4 Update the `docs/architecture.md` Mermaid diagram with the CANopen runtime observability
  path (runtime → logger/metrics, core → structured results) and verify the diagram renders and
  links resolve.

## 7. Verification

- [x] 7.1 Build Debug and run the full CTest suite; verify 100% pass.
- [x] 7.2 Build Release (`-O3 -march=native -flto`) and run the full CTest suite including the
  latency gates; verify 100% pass.
- [x] 7.3 Build with `-DNEXIGEN_ENABLE_TSAN=ON` and run the full CTest suite; verify no TSan
  reports, especially concurrent metrics harvesting.
- [x] 7.4 Build with `-DNEXIGEN_ENABLE_ASAN=ON` and run the full CTest suite; verify no ASan
  reports.
- [x] 7.5 Run clang-format, clang-tidy, and `openspec validate --strict`; verify all are clean.
- [x] 7.6 Run the observability benchmark with the allocation trap and verify the hot-path
  increments stay within the recorded budget with zero allocations.

## Workflow follow-up

- Mark `.scratch/canopen-observability/issues/01-logging-and-metrics.md` resolved with a pointer to
  this change and its spec after review.
- Archive the change after implementation and review are complete.
