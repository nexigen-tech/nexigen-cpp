# Tasks

## 1. Baseline and scaffolding

- [x] 1.1 Run `lizard src/runner/runner.cpp` and record baseline complexity for
  `Runner::stop` (32), `run_instance_supervisor` (27), and `validate_start_options` (19).
- [x] 1.2 Build Debug and run runner tests to establish a green baseline:  
  `cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build/debug --target nexigen_runner_tests && ctest --test-dir build/debug --output-on-failure -R runner`.

## 2. Decompose `validate_start_options`

- [x] 2.1 Extract `validate_artifact_dir` covering non-empty absolute path and byte limit.
- [x] 2.2 Extract `validate_settings_dirs` covering `global_settings_dir` and
  `local_settings_dir` absoluteness.
- [x] 2.3 Extract `validate_env_entries` covering environment key/value rules.
- [x] 2.4 Extract `validate_cpu_set` covering CPU set membership against online processors.
- [x] 2.5 Reduce `validate_start_options` to an orchestrator and verify all existing
  runner tests still pass.

## 3. Decompose `run_instance_supervisor`

- [x] 3.1 Extract `setup_exec_pipe` for `O_CLOEXEC` pipe creation and error reporting.
- [x] 3.2 Extract `launch_application_child` for child-side setup and `execvp`.
- [x] 3.3 Extract `await_exec_handshake` for parent-side pipe read.
- [x] 3.4 Extract `finalize_supervision` for `resource.json` write, child supervision,
  and `exit.json` write.
- [x] 3.5 Reduce `run_instance_supervisor` to an orchestrator and verify runner tests
  still pass.

## 4. Decompose `Runner::stop`

- [x] 4.1 Extract `resolve_stop_state` covering idempotent early-exit, identity gate,
  and already-dead instance fallback.
- [x] 4.2 Extract `send_instance_signal` covering `SIGTERM`/`SIGKILL` and `ESRCH` handling.
- [x] 4.3 Extract `wait_for_exit_or_settle` covering grace/settle waits.
- [x] 4.4 Extract `finalize_stop` covering `terminated_by` rewrite and fallback exit.
- [x] 4.5 Reduce `Runner::stop` to an orchestrator and verify runner tests still pass.

## 5. Extend unit tests

- [x] 5.1 Add or extend tests for each `validate_start_options` rejection path.
- [x] 5.2 Add or extend tests covering `Runner::stop` idempotency, signal escalation,
  and fallback exit paths.
- [x] 5.3 Build Debug and run runner tests; verify all new checks pass.

## 6. Verification and quality gates

- [x] 6.1 Build Release and run runner tests:  
  `cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/release --target nexigen_runner_tests && ctest --test-dir build/release --output-on-failure -R runner`.
- [x] 6.2 Build ThreadSanitizer and run runner tests:
  `cmake -B build/tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEXIGEN_ENABLE_TSAN=ON && cmake --build build/tsan --target nexigen_runner_tests && ctest --test-dir build/tsan --output-on-failure -R runner`.
- [x] 6.3 Build AddressSanitizer and run runner tests:
  `cmake -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNEXIGEN_ENABLE_ASAN=ON && cmake --build build/asan --target nexigen_runner_tests && ctest --test-dir build/asan --output-on-failure -R runner`.
- [x] 6.4 Run clang-format check:
  `cmake --build build/debug --target format-check` (or `format` to rewrite).
- [x] 6.5 Run `lizard src/runner/runner.cpp` and confirm:
  - `Runner::stop` complexity < 15;
  - `run_instance_supervisor` complexity < 15;
  - `validate_start_options` complexity < 15;
  - no newly introduced helper exceeds complexity 15.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
