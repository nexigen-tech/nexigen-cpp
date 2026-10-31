# Tasks

## 1. `core::ErrorCode::Conflict`

- [x] 1.1 Add `Conflict` as value 7 of `nexigen::core::ErrorCode` (append-only), update `to_string`/`from_string`, JSON and formatter coverage; update `tests/core/core_test.cpp` static asserts, catalogue array, name/JSON round-trips and formatter case. Verify: `nexigen_core_tests` passes and `static_cast<std::uint8_t>(ErrorCode::Conflict) == 7`.

## 2. Infrastructure API, Policy and heap-only factory

- [x] 2.1 Replace `include/nexigen/app/app.hpp` with `include/nexigen/app/infrastructure.hpp`: `Infrastructure<Policy, Tracer_capacity, Tracer_threads>` with the API from design D3, member order, accessors, `run`, stop state, private constructor and `make_infrastructure` friend. Verify: header compiles standalone under `-std=c++23 -fno-exceptions -fno-rtti -Werror`.
- [x] 2.2 Add the stateless policy concept and static asserts (`Logger`, `Metrics_sink`, `Trace_sink`), plus `make_infrastructure` returning `Result<unique_ptr<Infrastructure>>` via `new (std::nothrow)` (`SystemError` on failure). Verify: a test policy instantiates; a scratch TU declaring `Infrastructure` directly fails to compile; factory returns a non-null pointer.
- [x] 2.3 Rename `app_stop_reason.hpp` to `infrastructure_stop_reason.hpp` and `App_stop_reason` to `Infrastructure_stop_reason` (same values/JSON/formatter). Verify: enum contract tests pass; no `App_stop_reason` references remain.

## 3. Process-global signal ownership

- [x] 3.1 Add the non-template `alignas(64) std::atomic<const void*> signal_owner` and claim it at the start of `initialize` (CAS acq_rel) before `pthread_sigmask`; release it in `rollback` and at the end of `shutdown`. Verify: second initialization (same and different policies) returns `ErrorCode::Conflict`; after shutdown/failure a new initialize succeeds.
- [x] 3.2 Keep the existing initialization order and rollback semantics: claim → mask → Policy hooks → metric provider → tracer provider/initialize → watcher. Verify: failure at each hook releases ownership, restores the mask and leaves no live watcher.
- [x] 3.3 Keep `run`, `request_stop`, `stop_requested`, `stop_reason`, idempotent `shutdown`, flush order and destructor behavior, replacing all `App`/`current()` references. Verify: signal, first-stop-wins, double-shutdown and flush tests pass.

## 4. Application tests migration

- [x] 4.1 Rewrite `tests/app/app_test.cpp` around policies (`Test_policy`, `Fail_logger_policy`, `Fail_metrics_policy`, `Fail_trace_policy`, `Flush_fail_policy`) and `make_infrastructure`. Verify: all previous settings/lifecycle/signal/factory/coverage tests pass under the new API.
- [x] 4.2 Add tests for heap-only construction, `Conflict` on second initialize (same and different policies), ownership release after shutdown/failure, and `NotInitialized` factories. Verify: `nexigen_app_tests` passes.

## 5. Example and documentation

- [x] 5.1 Rewrite `examples/app/ex_app_lifecycle.cpp` with `Demo_policy` and `auto infra = make_infrastructure<Demo_policy>()`; keep the logger/metric/trace/SIGINT/shutdown-reason checks. Verify: `ctest -L examples` runs it with status 0.
- [x] 5.2 Update README and `docs/architecture.md`: composition, heap-only factory, `Conflict`, `Infrastructure_stop_reason`, removal of `App`/`current()`. Verify: the documented sample compiles/runs; module map stays accurate.
- [x] 5.3 Remove all `App<Derived,...>`, `App::current()` and `App_stop_reason` references from headers, tests, examples and docs. Verify: `grep` finds none outside the archive.

## 6. Verification

- [x] 6.1 Compile and pass 100% of unit tests and examples under Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify: `ctest --test-dir <dir> --output-on-failure` reports 100% in all four modes.
- [x] 6.2 Run the coverage target and keep overall line coverage ≥95% (current baseline 96.1%). Verify: `cmake --build build/coverage --target coverage` reports ≥95%.
- [x] 6.3 Run `openspec validate rework-app-as-infrastructure --strict`. Verify: command reports valid.

## Workflow follow-up

- Sync the `core/result` and `app/application` deltas into the main specs and archive the change after review.
