# Design

## Context

See `proposal.md` — Why. `align-exec-process-model-with-autosar` put the process
table, transition engine, and `StateClient` in `nexigen-em`; the suite supervisor
(`src/exec/suite_supervisor.cpp`) still launches applications directly and marks
them `Running` immediately. `nexigen_exec` is COM-free, so the suite supervisor
cannot host `StateClient` directly.

## Goals / Non-Goals

**Goals:**

- Give the suite supervisor the same readiness inbox, process table, transition
  engine, and `StateClient` receiver as `nexigen-em`.
- Make suite start enter each group’s `initial_state` through the transition
  engine.
- Fold `nexigen-em` into the same suite control path.
- Preserve the gate-OFF build and its file/non-reporting fallback.

**Non-Goals:**

- New AUTOSAR subsystems (PlatformFG-as-EM-group, PHM lifecycle events,
  Resource Groups, Trusted Platform, Deterministic Execution, ARXML, UCM,
  Network Management, Diagnostics, StateMachine, Suspend-to-RAM).
- Changing the state/process vocabulary introduced by the previous change.

## Decisions

### D1 — COM-free control interface in `nexigen_exec`

`include/nexigen/exec/control_host.hpp` defines an abstract `ControlHost` and a
`ControlHostFactory` function-pointer type. `nexigen_exec` owns only the global
factory slot and the suite supervisor’s use of the interface; it never includes
`nexigen_com`. The executable installs the factory before starting a suite, so
the forked supervisor child inherits it.

`ControlHost` exposes `poll()`, `request_state(group, state)`, `group_state`,
`transition_active`, `reset_groups`, `ready(app)`, `process_state(app)`,
`app_state(app)`, `set_start_handler`, `set_stop_handler`, `on_launched(app)`,
and `on_exited(app, code)`. The handlers let the suite supervisor keep ownership
of `ChildProcess`, artifacts, attempts, and restart policy while the control
host drives state transitions.

### D2 — COM-gated `nexigen_exec_control`

A new `nexigen_exec_control` static library (built only when
`NEXIGEN_ENABLE_COM=ON`) contains the concrete `ComControlHost`, which owns a
`com::Node`, a `FunctionGroupRegistry`, an `ExecutionStateTracker`, an
`ExecutionManager`, and a `StateClient`. `StateClient` owns the
`Nexigen/EM/State/<app>` listeners and the request/response channels; the
control host forwards `poll()` to `serve_once()`. `nexigen-exec` and
`nexigen-em` link this target and call `install_control_host_factory()`;
`nexigen_exec` itself links neither `nexigen_com` nor the control library.

### D3 — Suite supervisor integration

`RuntimeSuite` gains a `ControlHost* control`. After runtime apps are
initialized, the supervisor installs handlers that call `launch_app`/`stop` for
the named app, then requests each group’s `initial_state` through
`control->request_state`. `launch_all` remains the legacy path when no factory
is installed (gate OFF). Dependency release uses `control->ready(dep)` when a
control host exists; status still uses the existing `AppState`/`SuiteStatus`
mapping. Supervisor restarts call `control->on_launched`; exit handling calls
`control->on_exited`. A whole-suite restart calls `control->reset_groups()`
before re-requesting the initial states, so a stale confirmed state cannot skip
the relaunch.

### D4 — StateClient process handlers and instance directories

`StateClient` gains optional `StartHandler`/`StopHandler` callbacks. When set,
`start_app`/`stop_app` call them instead of `ExecutionManager` directly; the
tracker updates stay in `StateClient`. `FunctionGroupRegistry` exposes the
manifest instance id per application so the suite path derives
`<run_dir>/<app>/<instance>` rather than one process-wide instance id.

### D5 — `nexigen-em` becomes a suite front-end

`nexigen-em --config` starts a suite via `ExecutionManager::start_suite` and
returns once the supervisor owns it, exactly like `nexigen-exec start --config`.
The child supervisor hosts `StateClient` through the control host; the direct
per-app `ExecutionManager::start` loop in `tools/nexigen-em` is removed. The
service name is the standard `Nexigen/EM/StateCommand` channel.

### D6 — Initial state through the transition engine

The suite supervisor no longer starts the processes configured for
`initial_state` directly. It requests the state through `ControlHost`; the
shared `FunctionGroupTransition` performs stop/restart/start, dependency
ordering, readiness confirmation, and `Undefined`/timeout handling. `Off`
remains the manifest initial state.

### D7 — Gate-OFF fallback

When `NEXIGEN_ENABLE_COM=OFF`, no control factory is installed. The suite
supervisor keeps the existing `launch_all`/initial-state selection and
file-based/non-reporting behavior; reporting readiness is unavailable in that
configuration and is documented. No COM symbol enters `nexigen_exec`. The same
fallback applies when the COM host cannot create its process-wide command
channel (for example another suite already owns it): the factory returns null
and the supervisor runs the non-reporting path.

## Risks / Trade-offs

- **[Fork-safe COM control]** → create the control host inside the supervisor
  child (after fork), never in the CLI parent; the factory is a plain function
  pointer, not an object crossing the fork.
- **[Two lifecycle owners]** → the suite supervisor owns `ChildProcess`,
  artifacts, and restarts; the control host owns state transitions and
  readiness. Handlers are the only boundary, and `on_launched`/`on_exited` keep
  the tracker aligned after supervisor restarts.
- **[Gate-OFF behavior differs]** → explicitly documented as non-reporting-only;
  the change does not silently pretend readiness exists.
- **[Startup gating changes CLI timing]** → `nexigen-exec start --config` still
  returns after `suite.json`, but the suite may remain in a transition until
  readiness; status exposes the state.

## Migration Plan

1. Add `ControlHost`/factory and the COM-gated implementation; install the
   factory in `nexigen-exec`/`nexigen-em`.
2. Add StateClient handlers/instance lookup; keep the direct `nexigen-em` path
   behind a compile-time fallback until the suite path is green.
3. Wire the suite supervisor: handlers, initial-state requests, readiness-based
   dependencies, restarts/exits.
4. Fold `nexigen-em` to `start_suite`, update smoke tests and docs.
5. Run the four-config matrix and gate checks; gate OFF stays green.

## Open Questions

None blocking. The control host’s exact service name is the standard
`Nexigen/EM/StateCommand`; per-suite service customization is deferred.
