# Proposal

## Why

The previous change renamed the module namespace to `nexigen::exec`, but the `runner` name lives on in the codebase's vocabulary: the `Runner` class (`exec::Runner` reads redundantly), the `runner_logging.*` feature, the `runner_name`/`runner_control_thread`/`runner_logger_thread` manifest fields, the `runner.jsonl` log file, the `TerminatedBy::Runner`/`StreamKind::Runner` enum values, and prose "the runner". This leaves the module split between "exec" (namespace/directory/tool) and "runner" (every identifier inside it). This change renames every remaining `runner` identifier to `execution_manager`, completing the rename and giving the module one coherent vocabulary.

## What Changes

- **BREAKING**: class `Runner` → `ExecutionManager` (and all type references across `src/`, `tests/`, `tools/`, `include/`).
- **BREAKING**: files `runner.hpp` → `execution_manager.hpp`, `runner.cpp` → `execution_manager.cpp`, `runner_logging.hpp`/`runner_logging.cpp` → `execution_manager_logging.*`, `tests/exec/runner_test.cpp` → `tests/exec/execution_manager_test.cpp` (file-per-entity, snake_case of the type).
- **BREAKING**: enum values `TerminatedBy::Runner` → `TerminatedBy::ExecutionManager` and `StreamKind::Runner` → `StreamKind::ExecutionManager`, with their serialized wire strings `"runner"` → `"execution_manager"` (a data-format change in `exit.json`/stream-kind JSON).
- **BREAKING**: manifest/struct fields `runner_name` → `execution_manager_name`, `runner_control_thread` → `execution_manager_control_thread`, `runner_logger_thread` → `execution_manager_logger_thread`, `runner_control_cpus` → `execution_manager_control_cpus`, `runner_logger_cpus` → `execution_manager_logger_cpus` (and their JSON keys) — a manifest JSON format change.
- **BREAKING**: the runner-logging function surface `log_runner` → `log_execution_manager`, `warn_runner` → `warn_execution_manager`, `init_runner_logging`/`flush_runner_logging`/`shutdown_runner_logging` → `…_execution_manager_logging`, `load_runner_logging_settings` → `load_execution_manager_logging_settings`, `apply_runner_thread_spec` → `apply_execution_manager_thread_spec`, plus the internal state (`runner_logging_active`/`runner_logging_path`/`runner_logging_writer_mask`) and test fixtures (`RunnerFileLogger`, `Runner_pipe`, `Runner_fork`, `Runner_open_null`).
- **BREAKING**: runtime artifact names — the log file `runner.jsonl` → `execution_manager.jsonl` and the fallback data directory `.nexigen-run/` → `.nexigen-exec/`.
- Prose and spec text: "the runner"/"nexigen-runner" → "the execution manager"/"nexigen execution manager", and `Runner::start`/`Runner::stop`/`runner.jsonl`/`runner_name` mentions in spec text updated to match.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None — this change declares `skip_specs: true`. It is a pure identifier rename (class, functions, fields, files, serialized names) with no behavioral requirement change; the serialized-name changes (enum wire strings, manifest JSON keys, log file name) are part of the rename and the affected spec text is updated mechanically during implementation.

## Impact

- **Code**: ~54 files — `include/nexigen/exec/**` (class `runner.hpp`, `runner_logging.hpp`, `suite_manifest.hpp`, `suite_metadata.hpp`, `terminated_by.hpp`, …), `src/exec/**`, `src/hub/**`, `include/nexigen/hub/stream_kind.hpp`, `tests/exec/**`, `tests/hub/**`, `tools/nexigen-exec/main.cpp`.
- **Data format (breaking)**: enum wire strings (`"runner"` → `"execution_manager"` in `TerminatedBy`/`StreamKind`), manifest JSON keys (`runner_name`, `runner_control_thread`, `runner_logger_thread`, `runner_*_cpus`), and the log file `runner.jsonl`/data dir `.nexigen-run/`.
- **APIs**: `exec::Runner` → `exec::ExecutionManager`; `exec::runner_logging` free functions renamed. No signature/behavior change beyond the identifiers.
- **Specs**: `openspec/specs/exec/**`, `hub/discovery`, `core/affinity`, `examples`, `quality/coverage` — text mentions updated mechanically.
- **Performance / latency / allocation impact**: none — a pure identifier and serialized-name rename. No runtime, hot-path, memory, or latency change; the compiled code is equivalent (identifier renaming is erased at compile time, and the serialized-name changes are cold-path only).
