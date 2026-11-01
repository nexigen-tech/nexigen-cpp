# Tasks

## 1. Suite types and manifest

- [x] 1.1 Create `include/nexigen/runner/app_suite.hpp` with `Impact_on_app_suite_operability`, `Suite_state`, `App_state`, `App_spec`, `Suite_manifest`, `Suite_metadata`, `Suite_app_status`, and `Suite_status`; implement `to_string` member/free functions, `std::formatter`, and non-throwing `to_json`/`from_json`. Verify: serialization tests cover every enum and struct, including hostile JSON and round-trip.
- [x] 1.2 Implement `load_suite_manifest` and `validate_suite_manifest` in `src/runner/app_suite.cpp`, including defaults, safe-id checks, duplicate `(app, instance)`, settings-path rules, log-level spelling, CPU bounds, restart bounds, and the `.suite` reserved name. Verify: unit tests accept a complete manifest and reject each invalid case before any process starts.
- [x] 1.3 Register `app_suite` tests in `tests/CMakeLists.txt` and add manifest parsing cases (missing fields, wrong types, wrong version, unknown impact, unknown log level, empty apps, relative run_dir/artifacts_path, absolute local path, `..` in local path, duplicate CPU). Verify: `ctest -R nexigen_runner_tests` passes with the new cases.

## 2. Launch options for a single instance

- [x] 2.1 Add the `Start_options` overload to `Runner::start` and apply `env` entries in the child before the `NEXIGEN_*` identity block. Verify: tests assert manifest environment is visible and identity variables win over conflicting entries.
- [x] 2.2 Validate `cpus` in the parent and apply `sched_setaffinity` in the child; return `InvalidArgument` for out-of-range or duplicate indices before `fork`. Verify: a test reads the child's affinity and an invalid request creates no child.
- [x] 2.3 Add `artifact_dir`, `global_settings_dir`, `local_settings_dir`, and optional `log_level` to `Start_options`; extend `generate_settings` to accept a stream root; merge sources through `app::load_app_settings`, write the four settings files with artifact sink paths, and apply the effective level. Verify: tests cover suite global, application global replacement, local override, external sink override, explicit level override, and absent-level preservation.
- [x] 2.4 Extend the setup pipe to carry an `int32_t` status for `setsid`/`dup2`/affinity failures. Verify: a setup failure surfaces as `SystemError` and creates no recorded child.
- [x] 2.5 Add tests for environment precedence, affinity restriction, artifact directory contents, settings precedence, log-level precedence, and setup failure. Verify: the existing single-instance test suite still passes unchanged.

## 3. Suite metadata and startup

- [x] 3.1 Implement `suite_dir_path` plus atomic read/write of `suite.json`, `suite-state.json`, and `suite-exit.json`, including `global_settings_path`, `artifacts_path`, attempt number, and artifact directory fields. Verify: tests round-trip each document and assert no partial file is visible after a rewrite.
- [x] 3.2 Implement `start_suite` with the detached supervisor handshake: conflict check, `setsid`, `suite.json`, settings resolution, `attempt-0001` creation, first launch per app in manifest order, `suite-state.json`, and readiness report. Verify: a test starts a two-app suite, sees `suite.json`, and observes both apps and their `attempt-0001` directories after the caller exits.
- [x] 3.3 Implement `stop_suite` normal path via `stop.request`, including idempotence when `suite-exit.json` already exists. Verify: stop terminates every app, records `stopped`, triggers no restart, and a second stop succeeds.
- [x] 3.4 Implement `suite_status` derivation from `suite.json`, `suite-state.json`, `suite-exit.json`, instance files, and `/proc`. Verify: tests cover `running`, `stopped`, `failed`, and `unknown`.

## 4. Restart and impact policy

- [x] 4.1 Implement the 100 ms poll loop with abnormal-termination classification and delayed per-app retries for `degrade` and `not_impact`. Verify: crash tests observe a retry after `restart_delay_s`, code 0 does not retry, and exhausted retries mark only that app failed.
- [x] 4.2 Implement the `failure` path: stop all apps, wait `suite_restart_delay_s`, relaunch the whole suite with the next attempt directory for every app, and consume the shared `suite_restart_attempts` budget. Verify: tests show healthy apps restarted too, the failure app's own retry fields ignored, previous attempts untouched, and budget exhaustion producing `failed`.
- [x] 4.3 Implement deliberate-stop suppression through the suite-level `stop.request`, with the suite supervisor treating it as final and never restarting any application after it appears. Verify: a stop test sees no restart, and an already-stopped suite returns success.
- [x] 4.4 Ensure retries reuse the control instance directory, rewrite `resource.json`, clear the previous `exit.json`, regenerate settings, and create the next `attempt-<NNNN>` with new `stdout.log`, `stderr.log`, and four JSONL files. Verify: a restart test finds both attempt directories, distinct PIDs, and untouched files from the first attempt.
- [x] 4.5 Implement the forced-stop path when the suite supervisor is unresponsive: SIGTERM/SIGKILL the supervisor, stop each recorded instance, and write `suite-exit.json` with reason `forced_stop`. Verify: a test kills the supervisor and still stops every app.

## 5. CLI and documentation

- [x] 5.1 Add `start --config`, `stop --config|--suite-dir`, and `status --config|--suite-dir` to `tools/nexigen-run/main.cpp`, keeping the existing single-instance forms unchanged. Verify: `nexigen-run --help` documents both forms and a CLI test starts/stops a suite.
- [x] 5.2 Add `status --json` with the same aggregate and per-app state, attempt number, and artifact directory as text, and verify that `--config` and `--suite-dir` report identical values. Verify: JSON-shape test and route-parity test pass.
- [x] 5.3 Document the manifest schema, settings precedence, effective log level, artifact layout, impact levels, restart budgets, affinity/env rules, and CLI examples in `docs/runner-suite.md`. Verify: every documented example is a valid manifest and each command matches the implemented syntax.

## 6. Integration verification

- [x] 6.1 Build and run 100% of unit tests in Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify: all four CTest runs pass.
- [x] 6.2 Run `openspec validate nexigen-run-app-suite --strict`. Verify: the change validates cleanly.
