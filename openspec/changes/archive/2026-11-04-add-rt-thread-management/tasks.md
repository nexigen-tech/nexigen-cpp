# Tasks

## 1. Core CPU and thread vocabulary

- [x] 1.1 Implement fixed 1024-bit `core::CpuSet`, legacy mask/span conversions, duplicate/range validation, and canonical range formatting; verify core tests cover CPUs 64/1023/1024, empty sets, duplicates, negatives, offline-versus-structural validity, and exact 128-byte layout.
- [x] 1.2 Implement `SchedulingPolicy`, `ThreadApplyMode`, and 192-byte `ThreadSpec` with name/priority/scheduler-presence validation; verify default inheritance, explicit `Other,0`, FIFO/RR bounds, 0/15/16-byte names, and 192-byte layout in core tests.
- [x] 1.3 Implement member/free `to_string`, `std::formatter`, and non-throwing JSON for `CpuSet`, both enums, and `ThreadSpec`; verify string/JSON round-trips, unknown enum names, malformed JSON, and unchanged destinations on invalid input.
- [x] 1.4 Convert `core::affinity` into compatibility adapters over the shared CPU machinery while keeping module shims forward-only; verify existing affinity tests still pass and document the new public vocabulary in `docs/architecture.md` and the core README section.

## 2. Core application, diagnostics, and rollback

- [x] 2.1 Implement `apply_thread_spec_self` with copied cold specs, pre-change snapshots, name → affinity → scheduler order, and 64-byte `ThreadApplyReport`; verify first-work ordering and exact report/layout tests without changing any worker loop.
- [x] 2.2 Implement required-mode stop/restore and best-effort attempt-all semantics; verify deterministic partial reports, reverse-order restoration, no required fallback to `Other`, and no syscalls for a default spec.
- [x] 2.3 Extend existing `NEXIGEN_ENABLE_FAULT_INJECTION` cold hooks for name/affinity/scheduler and snapshot/restore calls; verify `EPERM`, `EINVAL`, multi-attribute failure, and rollback-failure tests run as an unprivileged user with production hooks compiled out.
- [x] 2.4 Add report `to_string`/formatter/JSON round-trip and startup publication tests using release/acquire readiness; verify no additional atomic operation appears after startup and document apply/rollback semantics in `docs/architecture.md`.

## 3. Logging integration and settings

- [x] 3.1 Migrate `FileLoggerConfig` and `ConsoleLoggerConfig` to `ThreadSpec`, apply a copied spec at the start of `AsyncWorker::run`, and publish the report through the existing startup surface; verify best-effort behavior remains asynchronous and sink-open/drain/flush/member-order tests remain unchanged.
- [x] 3.2 Implement required logger startup wait, stop/join, sink reset, and safe destruction/reinitialization; verify injected required affinity/FIFO failure returns `SystemError` before any console/file output.
- [x] 3.3 Migrate `LoggerOptions` and logging settings to canonical `worker_thread` with legacy mask/list/environment normalization and per-source precedence; verify CPUs above 63, explicit empty CPUs, console legacy-affinity isolation, and global/local/environment tests.
- [x] 3.4 Update logging config `to_string`/formatter/JSON, examples, and README settings schema; verify config round-trips and run the Debug and Release logging suites, including unchanged producer allocation and 20 ns/3 ns latency gates.

## 4. Metrics integration and settings

- [x] 4.1 Migrate `MetricProviderConfig` to `ThreadSpec` and apply it before the first harvest only when the interval enables a worker; verify zero-interval creates no worker, report publication, and unchanged fixed-rate wait/collection/destruction tests.
- [x] 4.2 Implement required constructor-start failure reporting with no running harvester and best-effort partial continuation; verify injected failures produce the expected `startup_status()` and safe destruction.
- [x] 4.3 Migrate metrics settings and config serialization to canonical `worker_thread` with legacy mask/environment compatibility; verify merge precedence, empty CPUs, complete JSON/to_string/formatter round-trips, example updates, and unchanged metrics latency gates in Release.

## 5. Tracing integration and settings

- [x] 5.1 Migrate `TracerProviderConfig` to `ThreadSpec`, apply it before `binding_.start`, and retain the existing drain/backoff/flush/shutdown paths; verify valid attributes precede first delivery and no tracing sink gains a thread.
- [x] 5.2 Implement required initialize failure and worker join/reset versus best-effort continuation; verify injected failures return `SystemError` before the drain loop and never-created/destroyed provider behavior remains safe.
- [x] 5.3 Migrate tracing settings and provider serialization to canonical `worker_thread` with legacy mask/environment compatibility; verify merge precedence, JSON/to_string/formatter round-trips, example updates, and unchanged 20 ns/2 ns tracing latency gates in Release.

## 6. Chrono integration and settings

- [x] 6.1 Migrate `ChronoProviderConfig` to `ThreadSpec`, apply it before the first snapshot, and preserve deferred start, zero-interval behavior, fixed-rate waits, and final snapshot/flush ordering; verify attribute ordering with a counting sink.
- [x] 6.2 Implement required `start()` failure with worker join/no snapshot loop and best-effort partial continuation; verify injected failures leave destruction and repeated start behavior deterministic.
- [x] 6.3 Migrate chrono settings/apply/serialization to canonical `worker_thread` with legacy mask/environment compatibility; verify merge precedence, JSON/to_string/formatter round-trips, example updates, and unchanged chrono suites in Debug and Release.

## 7. Infrastructure rollback

- [x] 7.1 Add required worker-start status checks at the existing metrics construction point and propagate logger/chrono/tracing required failures into the current rollback path; verify every injection point tears down earlier components in reverse dependency order and restores signal mask/ownership.
- [x] 7.2 Verify best-effort partial application does not trigger rollback, repeated initialization remains idempotent, and the signal watcher/configuration remains unchanged; update app initialization documentation with the new failure semantics and run app tests in Debug and Release.

## 8. Runner CPU and thread configuration

- [x] 8.1 Migrate `StartOptions::cpus`, `AppSpec::cpus`, and child pre-`exec` conversion to `CpuSet`; verify CPUs above 63, empty-set reset behavior, duplicate/offline rejection before fork, and unchanged launch/environment/argv behavior without assigning application policy or priority.
- [x] 8.2 Migrate suite manifest/metadata to canonical `runner_control_thread` and `runner_logger_thread` with legacy list compatibility and documented precedence; verify old manifests/suite directories load and new specs round-trip through JSON/to_string/formatters.
- [x] 8.3 Apply control specs on the calling thread before its control loop and logger specs through normal worker startup; verify best-effort warnings continue supervision and required failures restore prior control attributes and fail before applications launch.
- [x] 8.4 Update runner examples, `docs/runner-suite.md`, and README configuration documentation; verify runner logging/process-supervision/app-suite tests, fork/restart logger lifecycle, and that application affinity is unchanged by runner failures.

## 9. Integration and verification matrix

- [x] 9.1 Run the complete GCC Debug suite with `NEXIGEN_ENABLE_FAULT_INJECTION=ON` and the complete Release suite with `-O3 -march=native -flto` and strict latency gates; require 100% tests passed and no regression in logging, metrics, tracing, or diagnostics median budgets on dedicated hardware.
- [x] 9.2 Configure and pass portable Release with `NEXIGEN_NATIVE_ARCH=OFF`, TSan with `NEXIGEN_ENABLE_TSAN=ON`, and ASan/UBSan with `NEXIGEN_ENABLE_ASAN=ON`; require 100% tests passed and no sanitizer diagnostics.
- [x] 9.3 Configure and pass the Clang Debug suite with `-Werror`, run clang-tidy over all `src/*.cpp`, and run the packaging consumer smoke test; verify no new dependency or consumer compile definition is required beyond the existing Linux/glibc affinity requirements.
- [x] 9.4 Run the coverage build with fault injection and preserve the project ≥95% plus runner/hub ≥90% gates; add coverage for validation, partial application, and rollback branches rather than excluding them.
- [x] 9.5 Add opt-in `NEXIGEN_RT_INTEGRATION_TESTS=1` real-name/affinity/FIFO/RR tests for a configured `CAP_SYS_NICE` host and prove default CI passes without root/CAP_SYS_NICE; record the opt-in command and expected skip behavior in the test documentation.
- [x] 9.6 Run OpenSpec validation and inspect the final diff against the design inventory; verify no producer/SPSC/instrument/tick hot path, wait/backoff/flush algorithm, tracing/metric/chrono sink threading, or external dependency changed.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived result and the synchronized main specs.
