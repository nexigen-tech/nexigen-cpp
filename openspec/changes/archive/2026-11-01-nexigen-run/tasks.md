# Tasks

## 1. Runner module boilerplate

- [x] 1.1 Create `include/nexigen/runner/` headers and CMake target `nexigen_runner`. Verify: `cmake --build build/debug` succeeds and the library links.
- [x] 1.2 Add `tools/nexigen-run/main.cpp` for the CLI binary. Verify: `nexigen-run --help` prints usage and returns 0.
- [x] 1.3 Add `tests/runner/runner_test.cpp` test harness (same NX_CHECK style as other modules). Verify: `ctest -R nexigen_runner_tests` passes.

## 2. Resource_metadata / Exit_metadata

- [x] 2.1 Define `Resource_metadata` with fields: `run_id`, `app_name`, `instance_id`, `instance_dir`, `pid`, `start_time_rfc3339`, `start_time_mono_ns`, `version`. Add `Terminated_by` enum for exit metadata.
- [x] 2.2 Define `Exit_metadata` with fields: `exit_code`, `signal` (both `std::optional<std::int32_t>`), `uptime_s`, `terminated_by`.
- [x] 2.3 Implement `to_string`, free `to_string`, `std::formatter`, and non-throwing ADL `to_json`/`from_json` for both structs and the enum. Verify: tests cover round-trip and hostile JSON under `JSON_NOEXCEPTION=1`.
- [x] 2.4 Implement factory helpers: `Resource_metadata::from_identity(Run_identity, pid)` and `Exit_metadata::from_child_status(pid, status, uptime_s, terminated_by)`. Verify: basic bindings work.

## 3. Instance directory creation and resource.json

- [x] 3.1 Implement `create_instance_dir(identity, resolved)` that creates the full `<instance_dir>/settings/` tree. Verify: mkdir creates all levels.
- [x] 3.2 Implement `write_resource_json(identity, pid, dir)` that writes `resource.json`. Verify: file exists, valid JSON, matches round-trip assertion.
- [x] 3.3 Implement `write_exit_json(dir, exit_metadata)` that writes `exit.json`. Verify: same.
- [x] 3.4 Fail gracefully on permission errors: `ErrorCode::SystemError` if mkdir fails. Verify: tests try unwritable path.

## 4. Settings generation

- [x] 4.1 Implement `generate_settings(identity, resolved, instance_dir)` that writes four JSON files under `<instance_dir>/settings/`. Each file SHALL contain the absolute `sink.path` from resolved telemetry paths, and `log_root_directory` for logging. Verify: files exist, paths are absolute, `log_root_directory` matches instance_dir.
- [x] 4.2 Extend `Runner::start` to call settings generation before fork. Verify: integration test starts a child and checks the four settings files.

## 5. Process launch and supervision

- [x] 5.1 Implement `Runner::start(command, identity, timeout)`: create dir, write resource, fork, setsid in child, dup stdout/stderr, exec. Verify: child starts in separate session, stdout/stderr captured.
- [x] 5.2 Implement `Runner::stop(instance_dir, grace)`: kill(-pid, SIGTERM), wait grace seconds, SIGKILL if needed, write exit.json. Verify: normal stop writes exit.json with `"terminated_by": "runner"`.
- [x] 5.3 Implement `Runner::status(instance_dir)`: read resource.json, check pid via /proc, return `running`/`stopped`/`exited`/`unknown`. Verify: all four states observable.
- [x] 5.4 Idle timeout: start an internal timer; on expiry initiate the stop sequence. Verify: child with short timeout is terminated.
- [x] 5.5 Status and stop are idempotent: calling status or stop on a directory with no running child reads exit.json. Verify: double-stop does not signal.

## 6. CLI binary

- [x] 6.1 Implement `nexigen-run start` subcommand parsing environment variables and CLI flags. Verify: `nexigen-run start --app test --instance i1 -- /bin/sleep 10` starts a child, `nexigen-run status <dir>` returns `running`, `nexigen-run stop <dir>` terminates it.
- [x] 6.2 `stop` subcommand. Verify: can be called with instance-dir argument.
- [x] 6.3 `status` subcommand. Verify: prints state name.
- [x] 6.4 Error handling: missing arguments, bad paths, non-existent dir. Verify: appropriate exit codes.

## 7. Build and full verification matrix

- [x] 7.1 Debug build and test suite passes (`ctest --test-dir build/debug --output-on-failure`).
- [x] 7.2 Release build (including strict latency guards) passes (`ctest --test-dir build/release --output-on-failure`).
- [x] 7.3 ThreadSanitizer build passes (`ctest --test-dir build/tsan --output-on-failure`).
- [x] 7.4 AddressSanitizer + UBSan build passes (`ctest --test-dir build/asan --output-on-failure`).
- [x] 7.5 `openspec validate nexigen-run --strict` passes.

## Follow-up (not part of this change)

- nexigen-hub: inotify discovery, JSONL tailing, run/app/instance tree, UI/API.