# Tasks

All tasks are cold-path implementation work. The public headers, JSON schemas, and CLI
contract do not change. Compile the `nexigen_runner` target after each extraction step.

## 1. Baseline and seams

- [x] 1.1 Record the pre-refactor `lizard` baseline for `src/runner/app_suite.cpp` (1 file, 99 functions, average CCN 3.6, no function above 15).
- [x] 1.2 Create `include/nexigen/runner/detail/suite_launch_plan.hpp` with `SuiteLaunchPlan` and `resolve_suite_launch()`.
- [x] 1.3 Create `include/nexigen/runner/detail/suite_reporting.hpp` with the report builders/writers used across TUs.
- [x] 1.4 Create `include/nexigen/runner/detail/suite_supervisor.hpp` with `run_suite_supervisor()` and `suite_poll_interval_ms`.

## 2. Scenario resolution

- [x] 2.1 Create `src/runner/suite_manifest.cpp`; move the suite JSON helpers, all DTO `to_string`/`to_json`/`from_json` definitions, the path contract, the validation pipeline, and `load_suite_manifest` verbatim.
- [x] 2.2 Create `src/runner/suite_launch_plan.cpp`; move `command_vector` and implement `resolve_suite_launch()` with the existing settings-source, log-level, environment, CPU, argv, and identity rules.
- [x] 2.3 Verify `nexigen_runner` compiles and the manifest/JSON serialization tests still pass.

## 3. Reporting

- [x] 3.1 Create `src/runner/suite_reporting.cpp`; move `write_suite_status`, `write_suite_exit` file writing, `build_suite_metadata`, `write_suite_metadata_file`, `write_terminal_suite_state`, `suite_status`, and its derivation helpers.
- [x] 3.2 Change the supervisor-facing reporting API to accept `SuiteStatus` / `SuiteExit` / `SuiteMetadata` instead of `RuntimeSuite`; keep write order and failure handling unchanged.
- [x] 3.3 Verify `nexigen_app_suite_tests` status, stop, forced-stop, and serialization checks still pass.

## 4. Process orchestration

- [x] 4.1 Create `src/runner/suite_supervisor.cpp`; move `RuntimeApp`, `RuntimeSuite`, launch/stop, retry and suite-restart policy, startup sequence, logging initialization, and the supervisor loop.
- [x] 4.2 Replace inline `advance_app` branches with an `AppStateMachine` exposing `on_exit_consumed`, `on_restart_timer`, and `on_launch_failed`; preserve the scan-stop-on-suite-restart behavior.
- [x] 4.3 Make `launch_app` consume `SuiteLaunchPlan` from `resolve_suite_launch` and keep the attempt increment, launch-failure flags, and resource metadata read unchanged.
- [x] 4.4 Create `src/runner/suite_control.cpp`; move `preflight_suite_threads`, `suite_is_running`, `create_suite_pipes`, `run_suite_supervisor_child`, `start_suite`, `stop_suite`, and the stop-phase helpers.
- [x] 4.5 Delete `src/runner/app_suite.cpp`; confirm no include or build reference remains and `CMakeLists.txt` picks up the new TUs through the existing glob.

## 5. Behavior and quality verification

- [x] 5.1 `lizard` over the five new TUs: no function above CCN 15 and average complexity not above the recorded baseline.
- [x] 5.2 Debug: build all targets and run `nexigen_runner_tests`, `nexigen_runner_logging_tests`, and `nexigen_app_suite_tests`.
- [x] 5.3 Release (`-O3 -march=native -flto`): build the runner targets and run the same runner tests.
- [x] 5.4 AddressSanitizer: build the runner targets and run the same runner tests.
- [x] 5.5 ThreadSanitizer: build the runner targets and run the same runner tests.
- [x] 5.6 Run `clang-format` on every new and touched file.
- [x] 5.7 `openspec validate refactor-app-suite-layers --strict` passes.
- [x] 5.8 Update the README `nexigen::runner` section to name the new responsibility-focused implementation TUs (public headers remain the same).


## Verification note

- The full Debug suite (36/36) passes. Release, AddressSanitizer, and ThreadSanitizer
  were run against the runner targets and the three runner test binaries because the
  working tree contains an unrelated, pre-existing in-progress split of the metrics
  tests (`tests/metrics/*_test.cpp`, tracked by modified `tests/CMakeLists.txt`) that
  does not compile under the Release strict-latency macro. The runner matrix is
  3/3 green in each configuration.
