# Design

## Context

`src/runner/runner.cpp` implements the per-instance process supervisor. Three
functions dominate the file's complexity budget:

- `validate_start_options` (19) mixes artifact path, settings path, environment,
  and CPU set validation.
- `run_instance_supervisor` (27) performs setup-pipe creation, child fork,
  application-child setup, exec handshake, resource.json write, child supervision,
  and exit.json write.
- `Runner::stop` (32) handles idempotent early-exit, identity verification,
  SIGTERM/SIGKILL escalation, grace and settle waits, and fallback exit writing.

All three are cold-path control-plane functions, but their size makes reviews and
unit testing difficult. The public API (`Runner::start`, `Runner::stop`,
`Runner::status`) must remain unchanged.

## Goals / Non-Goals

**Goals:**

- Reduce the cyclomatic complexity of `Runner::stop`, `run_instance_supervisor`,
  and `validate_start_options` to under 15 each.
- Split each function into focused helpers with a single responsibility.
- Preserve exact error codes, signal behavior, file writes, and reaping semantics.
- Add or extend tests that exercise each decomposed path through the public API.
- Keep all new helpers in the anonymous namespace inside `src/runner/runner.cpp`.

**Non-Goals:**

- Changing the instance lifecycle protocol, stop semantics, or public `Runner`
  interface.
- Refactoring unrelated functions in `runner.cpp` (e.g., `Runner::start`).
- Introducing new abstractions beyond simple free functions.

## Decisions

### 1. `validate_start_options` decomposition

Split into:

- `validate_artifact_dir` — non-empty absolute path and byte limit.
- `validate_settings_dirs` — `global_settings_dir` and `local_settings_dir`
  absoluteness.
- `validate_env_entries` — environment key/value rules.
- `validate_cpu_set` — CPU set membership against online processors.

`validate_start_options` becomes an orchestrator calling the four helpers in order.

**Rationale:** Each helper owns one validation domain; the orchestrator preserves
the original short-circuit semantics.

### 2. `run_instance_supervisor` decomposition

Split into:

- `setup_exec_pipe` — create the `O_CLOEXEC` setup pipe; report system errors.
- `launch_application_child` — child-side `setsid`, fd redirection, affinity, env,
  and `execvp` with exec-correct handshake.
- `await_exec_handshake` — parent-side read from the setup pipe.
- `finalize_supervision` — write `resource.json`, call `supervise_child`, write
  `exit.json`.

`run_instance_supervisor` becomes: setup pipe → fork → child path / parent path.

**Rationale:** The child and parent paths are conceptually separate; splitting them
makes the fork-and-handshake protocol explicit. The helper functions are still
`[[noreturn]]` where appropriate.

### 3. `Runner::stop` decomposition

Split into:

- `resolve_stop_state` — read `resource.json`, identity gate, detect already-stopped
  instance, choose between signal path and fallback path.
- `send_instance_signal` — send `SIGTERM` or `SIGKILL`, ignoring `ESRCH`.
- `wait_for_exit_or_settle` — wait for `exit.json` + no group liveness within a
  deadline.
- `finalize_stop` — rewrite `terminated_by` or write fallback exit, return final
  status.

`Runner::stop` becomes an orchestrator: resolve → signal → wait → finalize.

**Rationale:** Stop logic has distinct phases; separating them makes the signal
escalation and settle logic testable through injected state where possible.

### 4. Keep helpers internal

All new helpers live in the existing anonymous namespace. No headers change.

**Rationale:** The helpers are implementation details of the runner lifecycle.
Exposing them would widen the module interface and couple tests to internals.

### 5. Test through the public API

Tests continue to call `Runner::start`, `Runner::stop`, `Runner::status`, and
`validate_start_options` (if needed via a public indirection). Where a helper
path cannot be exercised directly, we use existing fault-injection points and
crafted inputs rather than making helpers public.

**Rationale:** Avoids brittle `friend`/header tests and proves that the
composition still matches the original behavior.

## Risks / Trade-offs

| Risk | Mitigation |
|------|-----------|
| Signal timing regression in `stop` | Keep the exact `SIGTERM` → grace → `SIGKILL` → settle sequence and deadlines; run full runner test suite. |
| Exec handshake regression | Preserve the `O_CLOEXEC` pipe protocol byte-for-byte; add tests for setup failure codes. |
| Child reap regression | Keep `reap_child`, `waitpid` loops, and `supervise_child` semantics unchanged. |
| Large diff hard to review | Make the refactor mechanical: move code blocks into helpers without rewriting logic in the first commit. |

## Open Questions

None.
