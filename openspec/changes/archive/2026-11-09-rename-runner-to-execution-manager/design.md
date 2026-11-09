# Design

## Context

The module is already `nexigen::exec` (namespace, directories, CMake target, tool). The only remaining `runner` vocabulary is inside the module: the `Runner` class (`include/nexigen/exec/runner.hpp` + `src/exec/runner.cpp`), the `runner_logging` feature (`runner_logging.hpp`/`.cpp`), the `runner_*` manifest/struct fields, the `TerminatedBy::Runner` and `StreamKind::Runner` enum values, the `runner.jsonl` log file, the `.nexigen-run/` fallback dir, the `tests/exec/runner_test.cpp` file, and prose. The rename target `ExecutionManager`/`execution_manager` collides with nothing.

## Goals / Non-Goals

**Goals:**
- One coherent vocabulary: `ExecutionManager` (type) and `execution_manager` (snake_case identifiers, file names, serialized names) replace every `Runner`/`runner` identifier in the module and its consumers.
- A mechanical, compiler-enforced sweep: any missed `Runner`/`runner` reference is a compile error.

**Non-Goals:**
- No behavioral, signature, or hot-path change — identifiers and serialized names only.
- Do NOT touch `NEXIGEN_RUN_DIR`/`NEXIGEN_RUN_ID` (the "run identity" noun) or the already-renamed `nexigen::exec`/`nexigen-exec`/`NEXIGEN_EXEC_*` names.

## Decisions

**D1 — `skip_specs`, mechanical spec-text update.** The rename has no behavioral requirement change, so there are no delta specs. The serialized-name changes (enum wire strings, manifest JSON keys, log file) are part of the identifier rename and are documented as breaking; the affected spec text (`Runner::start`/`Runner::stop`, `runner.jsonl`, `runner_name`, "the runner") is updated mechanically during implementation, matching the namespace-rename precedent.

**D2 — The substitution map.** The sweep applies these exact transformations, in order (longest/most-specific first), to `include/`, `src/`, `tests/`, `examples/`, `tools/`, and `openspec/specs/`:

| From | To |
|---|---|
| `RunnerFileLogger` | `ExecutionManagerFileLogger` |
| `Runner_pipe` / `Runner_fork` / `Runner_open_null` | `ExecutionManager_pipe` / `ExecutionManager_fork` / `ExecutionManager_open_null` |
| `Runner` (word-boundary) | `ExecutionManager` |
| `runner_control_thread` | `execution_manager_control_thread` |
| `runner_logger_thread` | `execution_manager_logger_thread` |
| `runner_control_cpus` | `execution_manager_control_cpus` |
| `runner_logger_cpus` | `execution_manager_logger_cpus` |
| `runner_logging_active` / `runner_logging_path` / `runner_logging_writer_mask` | `execution_manager_logging_*` |
| `runner_logging` (and file names `runner_logging.hpp`/`.cpp`) | `execution_manager_logging` |
| `runner_name` | `execution_manager_name` |
| `runner_log_path` / `runner_log` / `runner_stopped` | `execution_manager_log_path` / `execution_manager_log` / `execution_manager_stopped` |
| `runner.jsonl` | `execution_manager.jsonl` |
| `.nexigen-run/` | `.nexigen-exec/` |
| `log_runner` / `warn_runner` | `log_execution_manager` / `warn_execution_manager` |
| `init_runner_logging` / `flush_runner_logging` / `shutdown_runner_logging` | `init_/flush_/shutdown_execution_manager_logging` |
| `load_runner_logging_settings` | `load_execution_manager_logging_settings` |
| `apply_runner_thread_spec` | `apply_execution_manager_thread_spec` |
| `runner` (remaining prose word) | `execution manager` (spaces) where prose; `execution_manager` where an identifier |

`describe_thread_apply_report` has no `runner` and stays.

**D3 — File relocation via `git mv`.** `include/nexigen/exec/runner.hpp` → `execution_manager.hpp`, `src/exec/runner.cpp` → `execution_manager.cpp`, `include/nexigen/exec/runner_logging.hpp` → `execution_manager_logging.hpp`, `src/exec/runner_logging.cpp` → `execution_manager_logging.cpp`, `tests/exec/runner_test.cpp` → `execution_manager_test.cpp`. CMake source globs pick up the moved `.cpp`; the explicit `tests/exec/runner_test.cpp` path in `tests/CMakeLists.txt` is updated.

**D4 — Ordering.** Single compiling unit: relocate files → apply the substitution map to code → apply it to spec text → update CMake test path → grep gate → full build gates.

## Risks / Trade-offs

- [A ~54-file sweep can miss an identifier] → compiler/linker enforce completeness; the task list carries an explicit grep gate for `Runner`, `runner_`, `runner.jsonl`, `.nexigen-run`, and `runner_logging`.
- [Serialized-name changes break existing on-disk manifests/`exit.json`] → the library has no released external consumers; the proposal marks these BREAKING, and the gate confirms no stale `"runner"` wire string remains.
- [Prose "runner" vs identifier `runner` ambiguity] → the map replaces identifiers first (word-boundary), then prose "the runner" → "the execution manager" as a final prose pass; the gate distinguishes remaining allowed `NEXIGEN_RUN_*` (run identity) from disallowed `runner` identifiers.

## Migration Plan

1. Land as one compiling unit (D3 relocation → D2 sweep → D4 gates).
2. Rollback = revert the single change; no data/format migration beyond the documented string changes.

## Open Questions

None.
