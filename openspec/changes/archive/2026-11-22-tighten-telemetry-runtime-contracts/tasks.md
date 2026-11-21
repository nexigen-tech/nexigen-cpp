# Tasks

## 1. Fallible infrastructure accessors

- [x] 1.1 Change `logger()` and `metrics()` to return `core::Result<Logger*>` / `core::Result<MetricProvider*>` gated on `initialized_`; verify pre-init and post-shutdown calls return `NotInitialized`
- [x] 1.2 Migrate every `logger()` call site in `tests/`, `examples/`, and tools to check the `Result`; verify the build has no unchecked dereference
- [x] 1.3 Migrate every `metrics()` call site to the `Result` form and verify the examples still run
- [x] 1.4 Update `README.md` and `docs/architecture.md` accessor examples; verify the documented snippets match the new signatures
- [x] 1.5 Add app tests for pre-init and post-shutdown `logger()`/`metrics()` rejection while issued handles remain usable; verify under ASan

## 2. Configuration-only diagnostics binding

- [x] 2.1 Change `MetricProvider::bind_diagnostics` and its templated overload to return `core::Status`, rejecting with `Conflict` when the harvester is running or the provider is sealed
- [x] 2.2 Change `TracerProvider::bind_diagnostics` and its templated overload the same way for the consumer/sealed state
- [x] 2.3 Migrate `bind_diagnostics` call sites (provider tests, infrastructure chrono binding is unchanged) to check the returned status
- [x] 2.4 Add tests for configuration binding success, live rebind rejection (`Conflict`, previous binding kept) and sealed rejection
- [x] 2.5 Update Doxygen comments with the configuration-mode contract and verify the `nexigen_metrics`/`nexigen_tracing` builds

## 3. Trace daemon requires the time base

- [x] 3.1 Remove the startup `sample_utc_offset` call and the `startup_utc_offset_ns_`/`fallback_reported_` state; require `TimeBaseClock::attached()` or attach `/nexigen-timebase` in `start()` and fail on error
- [x] 3.2 Change `resolve_snapshot_utc` to `std::optional<std::int64_t>` using only the published base; make `write_snapshot` fail with `NotInitialized` when unresolved and count the failure in `dump_failures_`
- [x] 3.3 Update the daemon tests: install a published base before a session, assert start fails without one, assert an unresolved trigger writes no file and counts a failure, and assert the `clock_gettime` interposer sees no `CLOCK_REALTIME` read
- [x] 3.4 Update `docs/trace-daemon.md`; verify the documented startup requirement and unresolved-trigger behavior match the tests
- [x] 3.5 Update Doxygen comments on `TraceDaemonSession::start`/`write_snapshot` and verify the `nexigen_tracing` build

## 4. Integration verification

- [x] 4.1 Build and run 100% of unit tests in Debug and Release (`-O3 -march=native -flto`) with latency gates via `scripts/build.sh build-debug build-release-com` and `ctest`
- [x] 4.2 Build and run 100% of unit tests under AddressSanitizer and ThreadSanitizer; verify no reports in core, chrono, metrics, tracing, app, and exec suites
- [x] 4.3 Run the lifecycle, CLI-settings and trace-daemon examples; verify the new accessor pattern and daemon base requirement
- [x] 4.4 Run `openspec validate tighten-telemetry-runtime-contracts --type change --strict` and verify the change validates after implementation

## Workflow follow-up

- Archive the change once review and CI requirements are satisfied.
- The `chrono()` lifetime exception remains a separate follow-up decision.
