# Tasks

Stage 1 only: header-per-entity + helper deduplication. All moves are copy-preserving; no behavior
change beyond the documented `StartReport` SIGPIPE-shield consistency. Each task compiles the
affected target before moving on.

## 1. Suite vocabulary headers

- [x] 1.1 Create `include/nexigen/runner/app_spec.hpp` with `struct AppSpec` + its `to_string`/JSON declarations, moved verbatim from `app_suite.hpp`.
- [x] 1.2 Create `include/nexigen/runner/suite_manifest.hpp` with `struct SuiteManifest` + `load_suite_manifest`/`validate_suite_manifest`.
- [x] 1.3 Create `include/nexigen/runner/suite_app_metadata.hpp`, `suite_metadata.hpp`, `suite_exit.hpp`, `suite_app_status.hpp`, `suite_status.hpp` with one struct each (`suite_status.hpp` also keeps `suite_status()`).
- [x] 1.4 Create `include/nexigen/runner/suite_paths.hpp` (`suite_dir_path`, `attempt_dir_path`) and `suite_control.hpp` (`start_suite`, `stop_suite`).
- [x] 1.5 Reduce `app_suite.hpp` to a thin umbrella that includes the new headers; keep the original module-level doc comment.
- [x] 1.6 Update `app_suite.cpp` includes to the narrow headers it uses; verify `nexigen_runner` compiles.

## 2. Instance vocabulary headers

- [x] 2.1 Create `include/nexigen/runner/resource_metadata.hpp` and `exit_metadata.hpp` with one struct each + their serialization declarations.
- [x] 2.2 Create `include/nexigen/runner/instance_file_io.hpp` (`read_json_file`, `write_json_file`, template `read_json_into<T>`, `write_resource_json`, `write_exit_json`).
- [x] 2.3 Create `include/nexigen/runner/instance_layout.hpp` (`instance_file_path`, `create_instance_dir`, `create_artifact_dir`) and `instance_settings.hpp` (`generate_settings` overloads).
- [x] 2.4 Reduce `instance_lifecycle.hpp` to a thin umbrella that includes the new headers.
- [x] 2.5 Update `instance_lifecycle.cpp`, `runner.cpp`, `runner_logging.cpp`, `app_suite.cpp` includes; verify `nexigen_runner` compiles.

## 3. `StartOptions` / `Runner` split

- [x] 3.1 Create `include/nexigen/runner/start_options.hpp` with `struct StartOptions` + free `to_string`.
- [x] 3.2 Trim `runner.hpp` to `class Runner` only; include `start_options.hpp` + `suite_manifest.hpp`.
- [x] 3.3 Verify all `Runner` consumers (`tools/nexigen-run`, `tests/runner`, `hub`) compile unchanged through the umbrella/narrow headers.

## 4. Detail helper headers

- [x] 4.1 Create `include/nexigen/runner/detail/start_report.hpp` with `struct StartReport`; remove it from `sys_helpers.hpp` (keep `sys_helpers.hpp` including the new header for compatibility).
- [x] 4.2 Create `include/nexigen/runner/detail/cpu_validation.hpp` with `online_cpu_count()` and `cpus_within_online(const core::CpuSet&)`.
- [x] 4.3 Create `include/nexigen/runner/detail/path_utils.hpp` with `max_path_bytes`, `absolute_path`, `safe_id`, `has_parent_segment`, `current_directory`, `absolute_from_cwd`, `directory_of`, `join_path`, `instance_dir_path`.

## 5. Deduplicate helpers

- [x] 5.1 Move `child_exit` to `src/runner/coverage_flush.hpp` as `detail::child_exit`; delete the copies in `runner.cpp` and `app_suite.cpp`.
- [x] 5.2 Move `redirect_to_null` to `detail/sys_helpers.hpp`; delete `redirect_to_null` / `redirect_to_null_local` copies.
- [x] 5.3 Add `read_start_report` + `write_start_report` to `detail/sys_helpers.hpp`; delete the duplicated readers in `runner.cpp` / `app_suite.cpp` and route all report writes through `write_all_no_sigpipe`.
- [x] 5.4 Move `validate_env_entries` to `detail/sys_helpers.hpp`; replace `validate_app_environment` in `app_suite.cpp`.
- [x] 5.5 Move `absolute_path` / `safe_id` / `join_path` / `instance_dir_path` / `max_path_bytes` to `detail/path_utils.hpp`; delete local copies in `runner.cpp`, `app_suite.cpp`, `instance_lifecycle.cpp`.
- [x] 5.6 Move online-CPU probing/validation to `detail/cpu_validation.hpp`; delete local copies in `runner.cpp`, `app_suite.cpp`, `runner_logging.cpp`.
- [x] 5.7 Replace repeated `read_json_file` + `from_json` blocks in `app_suite.cpp` with `read_json_into<T>` where the boolean result is sufficient.
- [x] 5.8 Verify `nexigen_runner` compiles and `nexigen_runner_tests`, `nexigen_app_suite_tests`, `nexigen_runner_logging_tests` pass under Debug.

## 6. Complete the enum split

- [x] 6.1 Move `to_json`/`from_json` for `AppState` to `src/runner/app_state.cpp`.
- [x] 6.2 Move `to_json`/`from_json` for `SuiteState` to `src/runner/suite_state.cpp`.
- [x] 6.3 Move `to_json`/`from_json` for `ImpactOnAppSuiteOperability` to `src/runner/impact_on_app_suite_operability.cpp`.
- [x] 6.4 Move `to_json`/`from_json` for `TerminatedBy` to `src/runner/terminated_by.cpp`.
- [x] 6.5 Remove the moved definitions from `app_suite.cpp` / `instance_lifecycle.cpp`; verify the serialization tests pass.

## 7. Hygiene

- [x] 7.1 Remove the dangling comments left by the previous enum split (`app_suite.hpp`, `instance_lifecycle.hpp`).
- [x] 7.2 Remove unused includes exposed by the split (`<chrono>`, `<system_error>`, `<limits>`, `<pthread.h>`, `<vector>`, etc.) from the touched TUs.
- [x] 7.3 Run `clang-format` on all touched files.
- [x] 7.4 Update the README `nexigen::runner` API section to name the new per-entity headers and the two compatibility umbrellas.

## 8. Verification

- [x] 8.1 `openspec validate refactor-runner-structure --strict` passes with `skip_specs: true`.
- [x] 8.2 Debug: build all targets and run 100% of unit tests (`ctest --output-on-failure`).
- [x] 8.3 Release (`-O3 -march=native -flto`): build all targets and run 100% of unit tests.
- [x] 8.4 AddressSanitizer: build all targets and run 100% of unit tests.
- [x] 8.5 ThreadSanitizer: build all targets and run 100% of unit tests.
- [x] 8.6 Confirm no new warnings under `-Werror` and `lizard` CCN ≤ 15 for `src/runner` / `include/nexigen/runner`.
- [x] 8.7 Update this task list: all stage-1 tasks checked.
- [x] 8.8 Archive the change after review (not part of stage 1 implementation).
