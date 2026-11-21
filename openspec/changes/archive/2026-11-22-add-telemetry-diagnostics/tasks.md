# Tasks

## 1. Core diagnostics vocabulary

- [x] 1.1 Add `DiagnosticSource`, `DiagnosticKind`, and `DiagnosticEvent` headers with `to_string`/`from_string`/`std::formatter`/`to_json`/`from_json`; verify round-trip, formatting, and unknown-name rejection in `tests/core`
- [x] 1.2 Add `DiagnosticBinding`, the `DiagnosticLogger` concept, and `NullDiagnosticLogger`; verify a bound callback receives the event, a default binding is a no-op, and the concept accepts/rejects at compile time
- [x] 1.3 Verify `static_assert(sizeof(DiagnosticEvent) == 3)` and `sizeof(DiagnosticBinding) == 16`, and that emission allocates nothing with the allocation counter armed
- [x] 1.4 Update Doxygen comments on every new public header and verify the `nexigen_core` build has no Doxygen warnings

## 2. Time-base diagnostics

- [x] 2.1 Extend `TimeBaseClient::attach` with a defaulted `DiagnosticBinding` and emit one `Degraded` on failure before returning; verify a bootstrap callback receives it and a silent attach behaves as before
- [x] 2.2 Add `bind_diagnostics` (binding + templated overload) and emit `Started` for a healthy client; verify late binding reports once
- [x] 2.3 Report `Degraded`/`Recovered` from `on_tick` on `Holdover`/`Lost` transitions only; verify one event per transition and none from conversion calls
- [x] 2.4 Update Doxygen comments on the changed `TimeBaseClient` surface and verify the `nexigen_chrono` build

## 3. Metric provider diagnostics

- [x] 3.1 Add `last_harvest_error_`, `last_reported_`, and `harvest_status()`; verify the status reflects the last collection without performing another collection or sink I/O
- [x] 3.2 Emit `Started`, `Degraded` (first transition), `Recovered`, and `Quiesced` through the binding, reporting outside `harvest_mutex_`; verify one event per transition and no events after quiescence
- [x] 3.3 Add the constructor overload that stores the binding before the harvester starts; verify no torn binding read under TSan
- [x] 3.4 Update Doxygen comments for the new constructor and diagnostics surface, and verify the `nexigen_metrics` build

## 4. Metrics UTC resolution and docs

- [x] 4.1 Add `utc_resolved` to `MetricBatchSnapshot`, resolve `utc_ns` through `TimeBaseClock::utc_from_canonical`, and remove `sample_utc_offset` from the provider; verify resolved and unresolved JSON round-trips
- [x] 4.2 Add a test that runtime telemetry paths perform no `CLOCK_REALTIME` read (fault injection or a `clock_gettime` interposer)
- [x] 4.3 Update `docs/metrics-guide.md` and the snapshot-format notes; verify the documented JSON example matches the serializer

## 5. Tracer provider diagnostics

- [x] 5.1 Add `last_delivery_error_`, `last_reported_`, and `delivery_status()`; verify the status reflects delivery/flush failures without draining or flushing
- [x] 5.2 Emit `Started`, `Degraded`, `Recovered`, and `Quiesced` from the consumer thread; verify one event per transition and none after quiescence
- [x] 5.3 Add the constructor binding parameter and verify binding before `initialize()` produces no race and no lost `Started`
- [x] 5.4 Update Doxygen comments for the diagnostics surface and verify the `nexigen_tracing` build

## 6. Trace-daemon UTC resolution

- [x] 6.1 Add `resolve_utc()` using the attached base with the startup offset as fallback, and report the fallback on stderr once per session; verify resolved naming, fallback naming, and one report
- [x] 6.2 Verify no `CLOCK_REALTIME` read occurs on the trigger path (interposer/fault injection) and that snapshots are still written when the base cannot resolve
- [x] 6.3 Update `docs/trace-daemon.md` and verify the documented naming/fallback behavior matches the tests

## 7. Application wiring

- [x] 7.1 Add `app::detail::BootstrapDiagnostics` (fixed buffer, `format_to_n`, `write(2)`, monotonic prefix) and pass it to `TimeBaseClient::attach`; verify an attach failure reaches stderr and startup still returns the `Status`
- [x] 7.2 Add `app::detail::DiagnosticForwarder<Logger>` and bind chrono/metrics/tracing to it in initialization order; verify the binding outlives the providers
- [x] 7.3 Add `logger_ready_` and `fail_logged()` so metric/tracing initialization failures are logged once before rollback; verify with a failing policy
- [x] 7.4 Add tests for the bootstrap attach failure, provider startup-failure logging, and late binding of a healthy client
- [x] 7.5 Update Doxygen comments in `infrastructure.hpp` and verify the `nexigen_app` build

## 8. Architecture and ADR

- [x] 8.1 Add `docs/adr/0003-telemetry-diagnostics.md` (channel, transition-only rule, no runtime `CLOCK_REALTIME`, ADR 0001/0002 links)
- [x] 8.2 Update `docs/architecture.md` with the diagnostics channel (Mermaid flow) and `docs/recorder.md` with the `resolved/unresolved` alignment; verify the documented flow matches the code
- [x] 8.3 Update `README.md` lifecycle notes with the bootstrap/bind order and verify the example still compiles

## 9. Integration verification

- [x] 9.1 Build and run 100% of unit tests in Debug and Release (`-O3 -march=native -flto`) with latency gates via `scripts/build.sh build-debug build-release-com` and `ctest`
- [x] 9.2 Build and run 100% of unit tests under AddressSanitizer and ThreadSanitizer; verify no ASan/TSan report in core, chrono, metrics, tracing, and app suites
- [x] 9.3 Run the lifecycle and trace-daemon examples; verify no runtime `CLOCK_REALTIME` read, no diagnostics after quiescence, and no sanitizer report
- [x] 9.4 Run `openspec validate add-telemetry-diagnostics --type change --strict` and verify the change validates after implementation

## Workflow follow-up

- Archive the change once review and CI requirements are satisfied.
- After archive, keep ADR 0001's "known deviations" list in sync: both entries should be marked resolved by this change.
