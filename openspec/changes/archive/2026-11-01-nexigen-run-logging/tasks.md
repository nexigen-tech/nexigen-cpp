# Tasks

## 1. Runner logging facade and global settings

- [x] 1.1 Add `include/nexigen/runner/runner_logging.hpp` and `src/runner/runner_logging.cpp` with a `FileLogger<64, 256, 4>` instantiation configured as `nexigen-run` / `runner.jsonl` / scheduled rotation `None`, plus `init`, `suspend`, `resume`, `flush`, `log`, `active`, and `shutdown`. Verify: unit tests write an info record, flush, and read one valid JSON line.
- [x] 1.2 Load `<global_settings_path>/logging.json` through the logging-settings loader and map `initial_log_level`, `flush_cpu_affinity`, `max_file_size_bytes`, and `spam_protection` throttle into the runner logger; keep path, file name, app name, and scheduled rotation runner-specific. Verify: tests cover applied values, missing file defaults, and invalid file warnings.
- [x] 1.3 Implement path and level resolution for a valid manifest, `suite.json` from `--suite-dir`, and the fallback: manifest level → global file level → `info`; suite-dir reads the recorded global settings; no manifest uses the working-directory fallback. Verify: tests assert the path and selected level for each route.
- [x] 1.4 Register the logging tests in `tests/CMakeLists.txt`. Verify: `ctest -R "nexigen_runner|nexigen_run"` passes with the new tests.
- [x] 1.5 Harden the logging producer's thread-local slot cache so a logger re-created at a reused address re-registers instead of returning an unpublished stale slot. Verify: an initialize/flush/shutdown/initialize/flush test sees records in both files.

## 2. Runner affinity and child isolation

- [x] 2.1 Add `runner_control_cpus` and `runner_logger_cpus` to `Suite_manifest` and `Suite_metadata`, including JSON round-trip and `suite.json` persistence. Verify: serialization tests cover both fields through manifest and suite metadata.
- [x] 2.2 Validate runner CPU lists softly: wrong JSON types reject the manifest, while duplicates, out-of-range indices, and logger indices above 63 produce warnings and disable that pin without failing validation. Verify: tests cover valid, duplicate, out-of-range, and type-error cases.
- [x] 2.3 Implement `apply_runner_affinity` and pin the CLI/suite-supervisor control thread and the FileLogger worker (manifest list, then `flush_cpu_affinity`, then OS placement). Verify: tests read the control thread affinity and the logger mask; invalid affinity leaves the thread unpinned with a warning.
- [x] 2.4 Reset affinity to all online CPUs in the suite-supervisor and per-instance-supervisor children, and in application children when `cpus` is empty; apply explicit application `cpus` afterwards. Verify: a pinned runner still launches an application with the full CPU set, while an application with `cpus` keeps that list.
- [x] 2.5 Add tests for control pinning, logger pinning, global-file fallback affinity, invalid affinity warnings, and application affinity isolation. Verify: `nexigen_app_suite_tests` passes.

## 3. CLI logging

- [x] 3.1 For single-instance and suite `start`, apply control affinity before the start fork, execute the start, then initialize logging with the resolved global settings and record the result. Verify: no logger worker exists at the start fork; a successful start leaves a `command` record naming the target.
- [x] 3.2 Resolve settings and affinity for `stop` and `status`, initialize logging, record command and result, and flush. Verify: text and JSON status both leave a record naming the state, and stop records its outcome.
- [x] 3.3 Handle invalid manifests and failures: use the fallback location with `info`, log a warning/error record, and keep the original exit code. Verify: an invalid manifest produces a fallback log record and a failed command still returns non-zero; `--help` creates no log.

## 4. Suite supervisor logging

- [x] 4.1 Reset inherited affinity, apply `runner_control_cpus`, launch initial attempts before logger initialization, then initialize logging with the loaded settings and record `suite_started` and initial application states. Verify: a started suite has the summary records and its control/logger pins.
- [x] 4.2 Suspend and join the logger before every retry or whole-suite relaunch, reinitialize afterwards, and record `app_launch`, `retry_scheduled`, and `suite_restart` events. Verify: restart tests find both pre-restart and post-restart records with attempt/state keys.
- [x] 4.3 Record application exits, launch failures, final app failure, normal stop, forced stop, and suite failure; flush and shut down before `_exit`. Verify: forced-stop and failure tests find the final event and no truncated JSON line.
- [x] 4.4 Make logging and affinity failures best-effort: unwritable primary path falls back, both locations failing leaves supervision running, invalid affinity does not stop it, and runtime flush errors do not alter policy. Verify: tests start and stop a suite with an unwritable artifact directory and invalid affinity and observe unchanged state.

## 5. Documentation

- [x] 5.1 Document runner logging and affinity in `docs/runner-suite.md`: path and fallback, level precedence (`manifest → logging.json → info`), operational settings, `runner_control_cpus`/`runner_logger_cpus`, soft failures, application isolation, fork-safe suspension, and append-only/no-scheduled-rotation behavior. Verify: every documented path, field, and command matches the implemented behavior.

## 6. Integration verification

- [x] 6.1 Build and run 100% of unit tests in Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify: all four CTest runs pass.
- [x] 6.2 Run `openspec validate nexigen-run-logging --strict`. Verify: the change validates cleanly.
