# Tasks

## 1. Application binary

- [x] 1.1 Scaffold `examples/suite/` with a `CMakeLists.txt` that registers `ex_suite_app` through `nexigen_add_example(ex_suite_app ex_suite_app.cpp nexigen::app)` and a CTest smoke entry named `ex_suite_app` labeled `examples`; verify `cmake --build` produces `ex_suite_app` and `ctest -N -R ex_suite_app` lists the test
- [x] 1.2 Implement `ex_suite_app.cpp` with a stateless policy (FileLogger plus the three JSON-file sinks, `Rotation None`), environment-driven role/timer parsing (`NEXIGEN_DEMO_ROLE`, `NEXIGEN_DEMO_EXIT_AFTER_S`, `NEXIGEN_DEMO_ABORT_AFTER_S`, `NEXIGEN_DEMO_LOG_PERIOD_MS`, `NEXIGEN_DEMO_RUN_FOREVER`), and `make_infrastructure` + `initialize(options, loaded)`; verify it builds cleanly under `-fno-exceptions` and `ex_suite_app --help` prints usage and exits 0
- [x] 1.3 Implement the six-instrument workload (counter, gauge, descriptive statistics, explicit histogram, exponential histogram, state gauge, all `MultiThreaded` + `ResetOnRead`) and the paced tick loop (1–2 logs/s), plus the per-role exit paths (worker bounded exit 0, worker forever until signal, degrade shutdown then exit 1, failure `std::abort()`); verify a bounded worker run exits 0 and writes `app.jsonl`/`metrics.jsonl`/`traces.jsonl`/`chrono.jsonl`, and a degrade-env run exits non-zero

## 2. Manifest and settings assets

- [x] 2.1 Add `examples/suite/settings/` with `logging.json`, `metrics.json`, `tracing.json`, `chrono.json` where metrics and chrono set `collect_interval_ns` to `1000000000`; verify the app settings loader reports OK with a one-second collection interval
- [x] 2.2 Add a per-app local settings source under `examples/suite/config/` that overrides one global field (for example a metrics capacity or log level); verify the merged settings reflect the local override through `load_app_settings`

## 3. Driver and cleanup scripts

- [x] 3.1 Add `examples/suite/run_suite_demo.sh` that derives an overridable absolute demo root, generates `suite.json` (three apps with `not_impact`/`degrade`/`failure`, each `--global-settings <instance_dir>/settings` in `args`, degrade `restart_attempts=3`, failure `suite_restart_attempts=1`), builds the example if needed, and runs `nexigen-run start --config`; verify the script starts the suite and `nexigen-run status --config` reports all three applications running
- [x] 3.2 Add `examples/suite/clean_suite_demo.sh` that removes the demo root idempotently; verify running it twice in a row exits 0 and removes the run, artifact, and suite metadata directories

## 4. Documentation

- [x] 4.1 Update the README Runnable Examples section to name `ex_suite_app` (seventeen executables), document `run_suite_demo.sh`/`clean_suite_demo.sh` and the directories they manage, and describe the expected restart → degrade → whole-suite-failure timeline; verify the documented commands run as written from a clean checkout

## 5. Verification

- [x] 5.1 Build and run the full build-mode matrix — Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) — running the examples CTest including `ex_suite_app`; verify 100% of tests pass with no sanitizer report
- [x] 5.2 Run the full manual scenario end to end: start the demo, observe the degrade instance retried then the suite `degraded`, the failure instance abort triggering a whole-suite restart, and the shared budget exhausting to suite `failed`, then stop and clean; verify `runner.jsonl` records `retry_scheduled`, `suite_restart`, and `suite_failed`, `attempt-*` directories increment, and `exit.json` shows an exit code for degrade and a signal for failure

## Workflow follow-up

- Archive the change after review requirements are satisfied.
- Verify the archived specs carry the `examples/suite-demo` capability and the `examples` delta.
