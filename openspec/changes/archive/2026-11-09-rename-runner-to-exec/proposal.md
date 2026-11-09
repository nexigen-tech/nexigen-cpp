# Proposal

## Why

`nexigen::runner` is a misnomer: the module does not "run" anything in the tooling sense — it owns child application execution and supervision (`Runner` forks/execs a detached supervisor, the suite supervisor launches/stops child apps and applies restart policy). The `runner` name also collides conceptually with the `Runner` class it contains and reads redundantly (`runner::Runner`). Renaming the namespace to `nexigen::exec` reflects its "execution / process supervision" role and, per the project's namespace↔directory↔CMake↔spec↔tool convention, aligns every layer of the module identity under one name.

## What Changes

- **BREAKING**: C++ namespace `nexigen::runner` → `nexigen::exec` across all 58 referencing files.
- **BREAKING**: module directories relocated `include/nexigen/runner/` → `include/nexigen/exec/`, `src/runner/` → `src/exec/`, `tests/runner/` → `tests/exec/` (file names stay snake_case of their types — `runner.hpp`, `runner.cpp`, `runner_logging.*` are unchanged because the `Runner` type and the runner-logging feature keep their names).
- **BREAKING**: CMake target `nexigen_runner` → `nexigen_exec`, alias `nexigen::runner` → `nexigen::exec`, source glob `NEXIGEN_RUNNER_SOURCES` → `NEXIGEN_EXEC_SOURCES`, test targets `nexigen_runner_tests`/`nexigen_runner_logging_tests` → `nexigen_exec_tests`/`nexigen_exec_logging_tests`, and install `EXPORT_NAME runner` → `exec`.
- **BREAKING**: CLI tool `nexigen-run` → `nexigen-exec` (`tools/nexigen-run/` → `tools/nexigen-exec/`, binary, `add_test` name `nexigen_run_help` → `nexigen_exec_help`, install rules). The run-identity env var `NEXIGEN_RUN_DIR` is NOT renamed — it denotes a suite "run", not the "runner" module; only its default tool path value `nexigen-run` → `nexigen-exec` changes.
- Spec capability paths relocated `openspec/specs/runner/{app-suite,instance-lifecycle,logging,process-supervision}/` → `openspec/specs/exec/...`, with the `nexigen::runner` namespace mentions in three of those specs updated to `nexigen::exec` (a mechanical path/text rename, not a behavioral change).
- **Not changed**: type names (`Runner`, `SuiteManifest`, `AppStateMachine`, …) and their file names; the `Runner` class and runner-logging surface keep their names — only the enclosing namespace and module paths move.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None — this change declares `skip_specs: true`. It is a pure namespace/module rename with no spec-level behavior change; the four existing `runner/*` capability directories are relocated to `exec/*` (and three of them get their `nexigen::runner` text mentions updated) as a mechanical filesystem rename during implementation, not as behavioral deltas.

## Impact

- **Code**: 58 files — `include/nexigen/runner/` (25 headers, plus a namespace mention in `include/nexigen/core/thread_management.hpp` and `include/nexigen/core/enum_traits.hpp` comment references), `src/runner/` (16 sources) + `src/hub/*` (2 files), `tests/runner/` (4 TUs) + `tests/hub/hub_test_support.hpp`, `tools/nexigen-run/main.cpp`, `examples/suite/CMakeLists.txt`. Header include directives `<nexigen/runner/...>` become `<nexigen/exec/...>`.
- **Build**: `CMakeLists.txt` (library + install/export), `tests/CMakeLists.txt` (4 test targets + `nexigen-run` CLI reference), `tools/CMakeLists.txt` (binary + install), `examples/CMakeLists.txt` (env var).
- **Specs**: `openspec/specs/runner/` → `openspec/specs/exec/` (4 capability directories relocated; 3 with namespace-text updates).
- **APIs**: `nexigen::runner::*` symbols resolve as `nexigen::exec::*`; downstream consumers (hub, app, tools, examples) update their qualifications. No type, function, or behavior signature changes.
- **Performance / latency / allocation impact**: none — this is a pure textual rename and directory relocation. No runtime code changes, no hot-path effect, no memory or latency change; the compiler emits byte-identical code (namespace qualification is erased at compile time).
