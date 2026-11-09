# Tasks

## 1. Directory relocation

- [x] 1.1 `git mv include/nexigen/runner include/nexigen/exec`, `git mv src/runner src/exec`, `git mv tests/runner tests/exec`. Verify: no `include/nexigen/runner`, `src/runner`, `tests/runner` paths remain.
- [x] 1.2 `git mv tools/nexigen-run tools/nexigen-exec`. Verify: `tools/nexigen-run` is gone, `tools/nexigen-exec/main.cpp` present.

## 2. Namespace and string sweep

- [x] 2.1 Replace `nexigen::runner` → `nexigen::exec` (both `namespace nexigen::runner` declarations and `nexigen::runner::` qualifications) and `#include <nexigen/runner/…>` → `#include <nexigen/exec/…>` across `include/`, `src/`, `tests/`, `examples/`, `tools/`. Verify: full configure + ninja compiles with no `nexigen::runner` symbol left.
- [x] 2.2 Update the comment-only namespace references in `include/nexigen/core/thread_management.hpp` and `include/nexigen/core/enum_traits.hpp` (`nexigen::runner::describe_thread_apply_report` → `nexigen::exec::describe_thread_apply_report`). Verify: the core library still compiles and the comment reference is accurate.

## 3. Build system

- [x] 3.1 In `CMakeLists.txt`: rename the library target `nexigen_runner` → `nexigen_exec`, alias `nexigen::runner` → `nexigen::exec`, glob variable `NEXIGEN_RUNNER_SOURCES` → `NEXIGEN_EXEC_SOURCES`, the source glob path `src/runner/*.cpp` → `src/exec/*.cpp`, `nexigen_precompile(nexigen_runner)` → `nexigen_precompile(nexigen_exec)`, the hub dependency `nexigen_runner` → `nexigen_exec`, and install `EXPORT_NAME runner` → `exec`. Verify: library and hub build.
- [x] 3.2 In `tests/CMakeLists.txt`: rename test targets `nexigen_runner_tests` → `nexigen_exec_tests` and `nexigen_runner_logging_tests` → `nexigen_exec_logging_tests`, update `runner/` source paths → `exec/`, and the CLI `$<TARGET_FILE:nexigen-run>` → `$<TARGET_FILE:nexigen-exec>`. Verify: all runner/app/hub test targets build.
- [x] 3.3 In `tools/CMakeLists.txt`: rename `nexigen-run` → `nexigen-exec` (add_executable, target_link_libraries, include dirs, `nexigen_run_help` → `nexigen_exec_help`, install list). Verify: the tool builds and `nexigen-exec --help` runs.
- [x] 3.4 In `examples/CMakeLists.txt`: the `NEXIGEN_RUN_DIR` env value's default tool path `nexigen-run` → `nexigen-exec` (the env var name `NEXIGEN_RUN_DIR` stays — it is a run-identity term). Verify: example targets configure and build.

## 4. Spec capability relocation

- [x] 4.1 `git mv openspec/specs/runner openspec/specs/exec` (4 capabilities: `app-suite`, `instance-lifecycle`, `logging`, `process-supervision`). Verify: no `openspec/specs/runner` remains and all 4 spec files live under `openspec/specs/exec/`.
- [x] 4.2 Replace `nexigen::runner` → `nexigen::exec` in the spec text of `exec/app-suite`, `exec/logging`, `exec/process-supervision` (the three that mention the namespace). Verify: `openspec validate --specs` passes.

## 5. Grep gate

- [x] 5.1 Confirm zero remaining hits across `include/`, `src/`, `tests/`, `examples/`, `tools/`, `CMakeLists.txt`, `cmake/`, and `openspec/specs/` for: `nexigen::runner`, `nexigen_runner`, `NEXIGEN_RUNNER`, `nexigen-run` (word-boundary; the prose noun "nexigen-runner" is allowed), `nexigen/runner/`, `src/runner/`, and `openspec/specs/runner`. (`NEXIGEN_RUN_DIR`/`NEXIGEN_RUN_ID` are run-identity terms and legitimately remain; archived changes and `openspec/changes/archive/` may retain history references.) Verify: the grep returns nothing outside archive.

## 6. Full gate

- [x] 6.1 Debug build: CMake configure + ninja, 100% of unit tests pass (`ctest`), including the renamed `nexigen_exec_tests`, `nexigen_exec_logging_tests`, `nexigen_app_suite_tests`, `nexigen_child_process_tests`.
- [x] 6.2 Release build `-O3 -march=native -flto`: 100% tests pass with latency gates unchanged (namespace qualification is erased at compile time — byte-identical output).
- [x] 6.3 ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`): runner/app/hub/child-process tests race-free.
- [x] 6.4 AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`): no leaks/races in the renamed module.
- [x] 6.5 `openspec validate "rename-runner-to-exec" --strict` passes (skip_specs: true, no delta specs required).

## Workflow follow-up

- Archive after review; `skip_specs: true` means the archive performs no spec-update operations (the spec path relocation was done in task 4 as a `git mv`).
