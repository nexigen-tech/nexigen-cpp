# Tasks

## 1. Probe helper

- [x] 1.1 Add `core::probe_thread_spec(spec)` to `include/nexigen/core/worker_thread.hpp` (one-shot `WorkerThread`, empty body, status return) and verify in `nexigen_core_worker_thread_tests` that a required failure is returned, a best-effort partial application succeeds, the caller's affinity is unchanged, and the probe thread is joined

## 2. PHM

- [x] 2.1 Add `bg_thread` and `fault_storage_thread` (`core::ThreadSpec`) to `PhmManifest`, include both in `to_json`/`from_json`/`to_string`, and make the daemon pass `effective_thread_spec(fault_storage_thread, bg_thread)` to `FaultStorage`; verify the manifest JSON round-trip test and that an old manifest without the fields still parses
- [x] 2.2 Migrate `phm::FaultStorage` from `std::thread`/`shutdown_` to `WorkerThread` with a stop-token loop, keep the queue drain and file append unchanged, and give the constructor a `ThreadSpec` parameter defaulted to empty; verify `nexigen_phm_tests` pass and records are still written after the migration

## 3. SM

- [x] 3.1 Add `bg_thread` and `audit_thread` to `SmManifest` with JSON/`to_string`, and make the daemon pass `effective_thread_spec(audit_thread, bg_thread)` to `TransitionAudit`; verify the manifest round-trip test and backward-compatible parsing
- [x] 3.2 Migrate `sm::TransitionAudit` from `std::thread`/`shutdown_` to `WorkerThread` with a stop-token loop, keep the ring drain/format/flush unchanged, and default the constructor spec to empty; verify `nexigen_sm_tests` pass and transition records are still written

## 4. Application signal watcher

- [x] 4.1 Add `core::ThreadSpec signal_thread` to `app::Settings` with JSON/diagnostic coverage, migrate `SignalWatcher` to `WorkerThread` (poll loop as `run`, existing double-start guard preserved), and pass the spec from `Infrastructure::initialize`; verify `nexigen_app_tests` pass, an explicit spec is applied on the watcher thread, and the fd is closed on a start failure

## 5. Metrics daemon HTTP

- [x] 5.1 Add `http_thread` (`core::ThreadSpec`) to `MetricDaemonConfig` with JSON/validation/diagnostics and fallback to `bg_cpu_affinity`; migrate `HttpFront`/`start_http`/`stop_http` to `WorkerThread` with a `std::stop_callback` bridging the server's stop flag; verify the config round-trip test, `nexigen_metrics_daemon_tests`, and `nexigen_metricsd_smoke` pass, including a clean stop/join before `stop_listen`

## 6. Log daemon main

- [x] 6.1 Add `main_thread` (`core::ThreadSpec`) to `LogDaemonConfig` with JSON/diagnostics, derive the effective spec from `main_thread` or the legacy `bg_cpu_affinity` `CpuSet`, and apply it on the daemon main thread before opening the subscriber; verify a `Required` failure exits non-zero before any port, the config round-trip test passes, and `nexigen_logd_smoke` remains green

## 7. Exec probes

- [x] 7.1 Replace the two inline `std::jthread probe` blocks in `src/exec/suite_control.cpp` and `src/exec/app_suite.cpp` with `core::probe_thread_spec`, preserving the `Required`-only preflight semantics; verify `nexigen_app_suite_tests`, `nexigen_em_orchestration_tests`, and the exec CLI tests pass, including the required-thread-failure case

## 8. Documentation

- [x] 8.1 Document the new per-thread fields and fallback rules in `docs/thread-management.md` and the daemon/README sections (`fault_storage_thread`, `audit_thread`, `signal_thread`, `http_thread`, `main_thread`, `bg_thread`), and note the `logd` legacy-affinity fix and `probe_thread_spec`; verify the documented names match the headers/manifests

## 9. Integration verification

- [x] 9.1 Run the full matrix — Debug, Release, TSan, ASan and a Clang `-Werror` build — and confirm 100% of tests pass, including the migrated phm/sm/app/metrics/logging suites and the new probe test
- [x] 9.2 Confirm the hot paths are unchanged: fault/audit loops, signalfd poll, HTTP epoll, log drain and backoff contain no new per-iteration work; verify the existing tracing lifecycle and trace-sink latency gates still pass in Release
- [x] 9.3 Run `openspec validate apply-thread-specs-platform-wide --strict` and the repository formatting/complexity gates; verify both are clean
- [x] 9.4 Run `graphify update .` after the code lands and confirm the knowledge graph is refreshed

## Workflow follow-up

- Archive the change after review. With it, every project-owned long-lived thread applies a configurable `ThreadSpec`; third-party (iceoryx2/libc) threads remain outside the contract.
