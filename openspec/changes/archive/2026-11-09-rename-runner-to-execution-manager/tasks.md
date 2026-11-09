# Tasks

## 1. File relocation

- [x] 1.1 `git mv include/nexigen/exec/runner.hpp include/nexigen/exec/execution_manager.hpp`, `git mv src/exec/runner.cpp src/exec/execution_manager.cpp`. Verify: no `runner.hpp`/`runner.cpp` remain.
- [x] 1.2 `git mv include/nexigen/exec/runner_logging.hpp include/nexigen/exec/execution_manager_logging.hpp`, `git mv src/exec/runner_logging.cpp src/exec/execution_manager_logging.cpp`, `git mv tests/exec/runner_test.cpp tests/exec/execution_manager_test.cpp`. Verify: no `runner_logging.*`/`runner_test.cpp` remain.

## 2. Identifier sweep (design D2 map)

- [x] 2.1 Apply the D2 substitution map to `include/`, `src/`, `tests/`, `examples/`, `tools/` (type `Runner` → `ExecutionManager`, `runner_*` fields/functions → `execution_manager_*`, fixtures `RunnerFileLogger`/`Runner_pipe`/`Runner_fork`/`Runner_open_null`, `runner.jsonl`, `.nexigen-run/`, logging function names). Verify: full configure + ninja compiles with no `Runner`/`runner_*` identifier left.
- [x] 2.2 Update `tests/CMakeLists.txt`: the test target source `exec/runner_test.cpp` → `exec/execution_manager_test.cpp`. Verify: `nexigen_exec_tests` builds.

## 3. Spec text update

- [x] 3.1 Apply the D2 map to `openspec/specs/` (`exec/**`, `hub/discovery`, `core/affinity`, `examples`, `quality/coverage`): `Runner::start`/`Runner::stop` → `ExecutionManager::…`, `runner.jsonl` → `execution_manager.jsonl`, `runner_name` → `execution_manager_name`, "the runner"/"nexigen-runner" prose → "the execution manager"/"nexigen execution manager". Verify: `openspec validate --specs` passes.

## 4. Grep gate

- [x] 4.1 Confirm zero remaining hits across `include/`, `src/`, `tests/`, `examples/`, `tools/`, `CMakeLists.txt`, and `openspec/specs/` for: `Runner` (word-boundary), `runner_` (identifier prefix), `runner.jsonl`, `.nexigen-run`, `runner_logging`, `log_runner`, `warn_runner`. (`NEXIGEN_RUN_DIR`/`NEXIGEN_RUN_ID` are run-identity terms and legitimately remain; `nexigen::exec`/`nexigen-exec`/`NEXIGEN_EXEC_*` are the already-renamed module identity.) Verify: the grep returns nothing outside archive.

## 5. Full gate

- [x] 5.1 Debug build: CMake configure + ninja, 100% of unit tests pass (`ctest`), including `nexigen_exec_tests` (renamed `execution_manager_test.cpp`), `nexigen_app_suite_tests`, `nexigen_child_process_tests`, `nexigen_hub_tests`.
- [x] 5.2 Release build `-O3 -march=native -flto`: 100% tests pass with latency gates unchanged.
- [x] 5.3 ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`): exec/app/hub/child-process tests race-free.
- [x] 5.4 AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`): no leaks/races in the renamed module.
- [x] 5.5 `openspec validate "rename-runner-to-execution-manager" --strict` passes (skip_specs: true, no delta specs required).

## Workflow follow-up

- Archive after review; `skip_specs: true` means the archive performs no spec-update operations (the spec text was updated in task 3.1).
