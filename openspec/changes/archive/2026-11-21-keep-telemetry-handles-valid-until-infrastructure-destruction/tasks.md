# Tasks

## 1. Logger quiescence

- [x] 1.1 Expose `AsyncWorker::stop() noexcept` (request stop + join, idempotent) and verify with a logger test that no consumer thread remains joinable after the call
- [x] 1.2 Add `logging/concepts/quiescable_logger_concept.hpp` and a `static_assert` for `NullLogger`, `ConsoleLogger`, `FileLogger`, `Iceoryx2Logger`, and a `CompositeLogger` instantiation; verify the assertion fails when `quiesce` is removed from one logger in a scratch TU
- [x] 1.3 Implement `quiesce()` for `NullLogger` (no-op `OK`), `ConsoleLogger`/`FileLogger`/`Iceoryx2Logger` (`flush()` then `worker_.stop()`), and `CompositeLogger` (forward, first failure wins); verify `tests/logging/*` covers pending-record delivery, `flush()` after quiescence returning `NotInitialized`, and sink closure exactly once
- [x] 1.4 Add a `tests/logging` case for repeated `quiesce()` (success, sink untouched) and a post-quiescence producer writing into a full ring (no block, `dropped()` grows, sink receives nothing)
- [x] 1.5 Update Doxygen comments (`@brief`, `@param`, `@return`, `@note`) on every new `quiesce()`/`stop()` declaration and verify `cmake --build` emits no Doxygen warnings for `nexigen_logging`

## 2. Metric provider quiescence

- [x] 2.1 Add `sealed_` and `quiesce() noexcept -> core::Status` to `MetricProvider` (stop harvester, final `collect_now()`, `flush()`, seal, return first error) and verify a unit test delivers one final batch before returning
- [x] 2.2 Gate `get_meter()`, `collect_now()`, and `flush()` on `sealed_` with `NotInitialized` while leaving `startup_status()` and worker reports readable; verify each gate in `tests/metrics/metric_provider_test.cpp`
- [x] 2.3 Make `~MetricProvider()` skip the final collection and sink flush when sealed, then run the destroy hooks and free the block; verify the sink batch count does not change across destruction after quiescence
- [x] 2.4 Add tests for idempotent `quiesce()`, sink-failure sealing (error returned, no second flush), post-quiescence instrument recording/reading under ASan, and meter creation rejection
- [x] 2.5 Update Doxygen comments for `quiesce()`, `sealed_`, and the changed control-plane contracts, and verify the `nexigen_metrics` build has no stale `@note`/`@warning` text

## 3. Tracer provider quiescence

- [x] 3.1 Add `sealed_` and `quiesce() noexcept -> core::Status` to `TracerProvider` (flush barrier, stop consumer, seal, publish `alive_ = false`) and verify accepted spans are delivered before the call returns
- [x] 3.2 Gate `get_tracer()` and `flush()` on the sealed state and keep `start_span` returning an inactive `NotInitialized` guard; verify in `tests/tracing/tracer_provider_test.cpp`
- [x] 3.3 Make `~TracerProvider()` skip both the join branch and the inline-drain `else if` when sealed; verify no duplicate delivery and no sink call during destruction after quiescence
- [x] 3.4 Add tests for idempotent `quiesce()`, an in-flight `ScopedSpan` ending after quiescence (no ASan report, never delivered), and post-quiescence diagnostics (`to_string`, drop snapshot)
- [x] 3.5 Update Doxygen comments for `quiesce()`, `sealed_`, and the dead-handle contract, and verify the `nexigen_tracing` build carries no stale lifetime warnings

## 4. Infrastructure lifetime and ordering

- [x] 4.1 Rewrite `Infrastructure::shutdown()`: remove the component `reset()` calls, quiesce tracing then metrics, flush then quiesce the logger, log tracer/metric quiescence failures while the logger is active, detach the time base last, restore the mask and release ownership; verify the order with `tests/app/infrastructure_test.cpp`
- [x] 4.2 Move the `meter()`/`tracer_for()` gates from optional presence to `initialized_`; verify both return `NotInitialized` after shutdown while `logger()`/`metrics()` stay usable
- [x] 4.3 Reorder `rollback()` so `logger_.reset()` happens before `TimeBaseClock::detach()`, and verify a failed-initialization test still passes
- [x] 4.4 Add app tests for post-shutdown handle use under ASan (counter, tracer, logger), stable sink batch count until `~Infrastructure`, idempotent shutdown, and the documented chrono exception
- [x] 4.5 Update `docs/architecture.md` (lifetime invariant, Mermaid lifecycle diagram), `docs/metrics-guide.md` real-time checklist, and the `README.md` lifecycle example; verify the documented order matches `shutdown()` and the example compiles
- [x] 4.6 Write `docs/adr/0002-telemetry-handle-lifetime.md` (borrowed handles, quiesce vs destroy, post-quiesce semantics per subsystem) and link it from `docs/architecture.md`

## 5. Integration verification

- [x] 5.1 Build and run 100% of unit tests in Debug and Release (`-O3 -march=native -flto`) with the latency gates via `scripts/build.sh build-debug build-release-com` and `ctest`; record the results
- [x] 5.2 Build and run 100% of unit tests under AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) and ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`); verify no ASan/TSan report in the metrics, tracing, logging, and app suites
- [x] 5.3 Run the lifecycle and integration examples that create meters/instruments and shut down; verify no sanitizer report and no sink I/O after `shutdown()` returns
- [x] 5.4 Run `openspec validate keep-telemetry-handles-valid-until-infrastructure-destruction --type change --strict` and verify the change still validates after implementation

## Workflow follow-up

- Archive the change once review and CI requirements are satisfied.
- The pre-existing test-fixture fix in `tests/support/test_time_base.hpp` (canonical validity window starts at the epoch so synthetic instants stay resolvable on long-uptime hosts) is test-support scope, not part of the behavior contract; mention it in the commit/PR.
- Open the follow-up change `add-telemetry-diagnostics`: `DiagnosticBinding`/`DiagnosticEvent` in `core`, `BootstrapDiagnostics` before the time-base attach, `bind_diagnostics` on chrono/metrics/tracing, initialization-failure logging in `Infrastructure`, and runtime checkpoints.
- In that follow-up, also migrate the ADR 0001 deviations: `MetricBatchSnapshot.utc_ns` and `trace_daemon_session` to `TimeBaseClock::utc_from_canonical` with a resolved/unresolved marker.
