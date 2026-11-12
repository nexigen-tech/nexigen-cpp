# Tasks

## 1. ThreadSpec helpers

- [x] 1.1 Add `ThreadSpec::has_requests()` and `core::effective_thread_spec(primary, fallback)` to `include/nexigen/core/thread_management.hpp`; verify tests cover the default (false), each individual attribute (name, CPUs, explicit `Other,0` scheduler), fallback selection for an empty primary, primary-wins, and that neither argument is mutated

## 2. WorkerThread

- [x] 2.1 Add `include/nexigen/core/worker_thread.hpp` with the `WorkerThread<Body>` template, the `WorkerBody`/`HasWorkerStart` concepts, the `alignas(64)` readiness cell (ready/code/mask/report with release/acquire publication), `start`/`request_stop`/`join`/`stop`, `joinable`/`ready`/`start_code`/`report`/`mask`, and the single-start `Conflict` contract; verify a started body runs on a different thread id exactly once and destruction joins it
- [x] 2.2 Implement the start sequence: self-apply `ThreadSpec` before any body hook, `Required` failure publishes the report/code/ready and never calls `on_start`/`run`, `BestEffort` continues, `on_start` runs before `run`, and the first error wins; verify with fault injection (scheduler rejection) that a required failure returns `SystemError`, leaves the body unrun and reports the rollback, while a best-effort partial application still runs
- [x] 2.3 Implement `on_start` error propagation and stop semantics: an `on_start` failure is recorded in `start_code` but `run` still executes; `request_stop` wakes a stop-aware condition, `join` completes, and stop/join are safe without a running thread; verify a CV body exits promptly on stop and that a second `start` returns `Conflict`
- [x] 2.4 Add `to_string`/free helper/`std::formatter`/`to_json` for `WorkerThread` (spec, ready, start code, report fields, mask) and deliberately no `from_json`; verify formatter parity, the JSON names every field, and a compile-time check rejects `from_json` for the live class

## 3. Provider migrations

- [x] 3.1 Migrate `logging::detail::AsyncWorker` to `WorkerThread`: remove `StartCell`/its `jthread`, move `sink.start()` into `on_start`, keep the drain/flush-barrier/backoff loop and shutdown drain in `run`, and delegate `writer_started`/`writer_code`/`writer_mask`/`writer_thread_report`; verify `nexigen_logging_tests` and the logging COM tests pass, including the required-failure and flush-barrier suites
- [x] 3.2 Migrate `tracing::TracerProvider`: remove `StartCell`/`start_worker`, start the wrapper from `initialize` after stamp calibration, move `binding.start` into `on_start`, keep `consume_loop` as `run`, and preserve the destructor inline-drain path for a never-started worker; verify `nexigen_tracing_tests` and `nexigen_tracing_com_tests` pass, including the required-thread-failure suite and the 20 ns lifecycle gate
- [x] 3.3 Migrate `chrono::Worker`: remove its `jthread`/startup atomics/report, keep the fixed-rate CV loop as `run`, make `start` idempotent via `joinable`, and delegate `startup_status`/`worker_mask`/`worker_thread_report`; verify `nexigen_chrono_tests` pass, including double-start and worker-affinity cases
- [x] 3.4 Migrate `metrics::MetricProvider`: replace `harvester_`/startup atomics with the wrapper, keep the fixed-rate harvest loop as `run`, preserve `startup_status` for the never-started case, and ensure a `Required` failure now rolls back and starts no loop; verify `nexigen_metrics_tests` pass, including the provider start and get_meter/harvester race suites, and add a required-failure assertion if absent

## 4. Build and test infrastructure

- [x] 4.1 Add `tests/core/worker_thread_test.cpp` as `nexigen_core_worker_thread_tests` (links `nexigen_core`, `Threads::Threads`, uses the deterministic fault-injection mechanism from `tests/core/core_test.cpp`); verify `ctest -N` lists it and the executable passes in Debug

## 5. Documentation

- [x] 5.1 Document `WorkerThread` in the README `nexigen::core` API section and `docs/thread-management.md`: the two-phase body, self-application invariant, readiness/`Required` semantics, `request_stop`/`join` contract, the "worker is the last member" rule, the diagnostic surface, and `has_requests`/`effective_thread_spec` as the stage-2 fallback foundation; verify the documented names match the headers and tests

## 6. Integration verification

- [x] 6.1 Run the full matrix with the change in place — Debug, Release, TSan, ASan and a Clang `-Werror` build — and confirm 100% of tests pass, including the new `nexigen_core_worker_thread_tests` and all four provider suites
- [x] 6.2 Confirm the hot path is unchanged: provider loop bodies, shard/ring drain, harvest cadence, flush barriers and backoff contain no new work; verify the existing tracing lifecycle (≤20 ns) and trace-sink `on_span` (≤400 ns) latency gates still pass in Release
- [x] 6.3 Run `openspec validate add-worker-thread --strict` and the repository formatting/complexity gates; verify both are clean
- [x] 6.4 Run `graphify update .` after the code lands and confirm the knowledge graph is refreshed

## Workflow follow-up

- Archive the change after review, then open `apply-thread-specs-platform-wide` for `phm`/`sm` `std::thread` conversion, `SignalWatcher`, metricsd HTTP, `logd` main affinity, the exec probe helper, and the per-thread manifest fields.
