# Design: Extract ChildProcess Class

## Context

Current implementation has two supervision layers:

1. **`runner.cpp`** — the low-level `Runner` facade that forks a detached
   supervisor, which forks and reaps the application child. It owns the
   fork/exec/signal machinery and is well-tested.
2. **`suite_supervisor.cpp`** — the suite loop that launches one instance
   supervisor per application and applies restart/impact policy. It kept a
   local `RuntimeApp` struct (state) plus an `AppStateMachine` class (policy),
   and reached for `Runner` directly on every start/stop.

The local `RuntimeApp` duplicated the same child bookkeeping (PID, supervisor
PID, attempt/restart counters, lifecycle state) that `Runner` already manages
implicitly through `resource.json`, and mixed it with the `AppStateMachine`
policy. See proposal.md for the motivation.

## Goals / Non-Goals

**Goals:**
1. Give the suite supervisor a single, named type (`ChildProcess`) for one
   child application's runtime state and lifecycle actions.
2. Eliminate the direct `Runner` plumbing scattered through
   `suite_supervisor.cpp` (`Runner runner; runner.start/stop(...)`).
3. Keep the change mechanical and low-risk: **no fork/exec/signal code moves
   out of `runner.cpp`**.

**Non-Goals:**
1. Do NOT reimplement or duplicate fork/exec/handshake logic — `ChildProcess`
   delegates to `Runner`.
2. Do NOT change the public `Runner` API.
3. Do NOT alter restart/impact policy (that stays in `AppStateMachine`).
4. No new concurrency model; each `ChildProcess` stays single-threaded.

## Decisions

### 1. Thin wrapper, not a reimplementation

**Decision**: `ChildProcess` stores the runtime state (the former `RuntimeApp`
fields) and adds exactly two behavior methods — `launch` and `stop` — which
delegate to `Runner::start` / `Runner::stop`.

**Rationale**: The fork/exec/supervisor machinery in `runner.cpp` is already
correct and covered by tests. Duplicating it would reintroduce the very
duplication this change removes. A thin wrapper concentrates the "which
process is this app, and how do I start/stop it" concern without moving
syscall-level code.

**Alternatives considered**:
- Full fork/exec reimplementation in `ChildProcess` → rejected: duplicates
  `runner.cpp` and risks regressions for no behavioral gain.
- `ChildProcess` as pure state struct with no methods → rejected: leaves the
  `Runner` plumbing scattered in `suite_supervisor.cpp`, the exact smell we
  are removing.

### 2. Ownership of the attempt counter

**Decision**: `ChildProcess::launch` does NOT bump `attempt`; the caller
(`launch_app`) does, because the attempt index feeds `resolve_suite_launch`
(artifact directory per attempt) before `launch` is called.

**Rationale**: The attempt counter is a launch-plan concern, not a child-lifecycle
concern. Keeping it in the caller avoids a hidden ordering dependency between
`++attempt`, `resolve_suite_launch`, and `Runner::start`.

### 3. Public value-type fields

**Decision**: `ChildProcess` exposes its state as public fields (mirroring the
former `RuntimeApp` struct) rather than getters/setters, and provides a small
query surface (`is_running`) plus `to_string`.

**Rationale**: The suite supervisor mutates state directly in several places
(`trigger_suite_restart`, `finish_failed`, `schedule_retry_or_fail`). Keeping
value-type fields means the diff is a mechanical rename (`RuntimeApp` →
`ChildProcess`), which minimizes risk. `AppSpec`/`SuiteManifest` already follow
this public-field value-type style in this codebase.

### 4. `RuntimeApp` becomes a type alias

**Decision**: In `suite_supervisor.cpp`, replace the local struct with
`using RuntimeApp = ChildProcess;` so the rest of the file compiles unchanged.

**Rationale**: The local name is referenced in ~20 places; a type alias makes
the integration a two-line change and keeps the diff reviewable. A follow-up
change can rename the alias to `ChildProcess` throughout if desired.

## Risks / Trade-offs

- **[Risk]** The wrapper adds a thin indirection layer over `Runner`.
  → **Mitigation**: methods are trivial inline-able calls; cold path only, no
  measurable cost.
- **[Risk]** `ChildProcess::launch` now owns the `resource.json` read (PID
  refresh) that `launch_app` previously did.
  → **Mitigation**: identical `read_json_into` + `ResourceMetadata` code, moved
  verbatim; covered by the existing `app_suite_test.cpp`.
- **[Risk]** Divergence if `Runner` behavior changes (new params, new files).
  → **Mitigation**: `ChildProcess` forwards to the same `Runner` methods the
  suite supervisor already called, so it inherits changes automatically.

## Migration Plan

Already implemented (single phase, mechanical):

1. Add `child_process.hpp` / `child_process.cpp`.
2. `using RuntimeApp = ChildProcess;` in `suite_supervisor.cpp`, delete the
   local struct.
3. Rewire `launch_app` and `stop_all` to call `app.launch(...)` / `app.stop(...)`.
4. Add `child_process.hpp` to the `app_suite.hpp` umbrella.

Rollback: revert the three file edits; `runner.cpp` is untouched, so a rollback
cannot affect single-instance supervision.
