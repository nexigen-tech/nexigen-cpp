# Proposal

## Why

`src/runner/runner.cpp` contains three functions with cyclomatic complexity well above
the maintainability threshold:

- `Runner::stop` — **32**
- `run_instance_supervisor` — **27**
- `validate_start_options` — **19**

These functions mix unrelated responsibilities: option validation, child setup,
handshake protocols, signal orchestration, process reaping, and idempotent stop
logic. A failure in one branch is expensive to debug, and adding new behavior
(e.g., a new stop policy or a new setup check) increases the risk of regressions.
We should decompose them into smaller, focused helpers while preserving exact
semantics and the public API.

## What Changes

- **Decompose `validate_start_options`** into:
  - `validate_artifact_dir`
  - `validate_settings_dirs`
  - `validate_env_entries`
  - `validate_cpu_set`
- **Decompose `run_instance_supervisor`** into:
  - `setup_exec_handshake` / `fail_exec_setup` helpers
  - `launch_application_child`
  - `await_exec_handshake`
  - `finalize_supervision` (write resource.json, supervise child, write exit.json)
- **Decompose `Runner::stop`** into:
  - `resolve_instance_resource`
  - `send_instance_signal`
  - `wait_for_instance_exit`
  - `write_stop_fallback_exit`
- Keep all public signatures unchanged.
- Keep all error codes and side effects (signals, file writes, reaping) identical.
- Add unit-test coverage for decomposed helpers where feasible through the public
  `Runner` API.

## Capabilities

### New Capabilities
None.

### Modified Capabilities
None. This is a pure internal refactor; no spec-level behavior changes.
`skip_specs: true` is set in `.openspec.yaml` for this change.

## Impact

- **Affected code:** `src/runner/runner.cpp` (internal implementation only).
- **Tests:** Extended or new tests in `tests/runner/runner_test.cpp`.
- **API/ABI:** No public API changes.
- **Performance:** No impact; runner lifecycle is cold-path control-plane code.
