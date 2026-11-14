# Design

## Context

`introduce-platformfg-process-model` added the `PlatformFG` vocabulary and a
SM-side offline sequence, but the EM never sees `MachineFG`. The previous
change (`unify-exec-suite-control-path`) gave the suite supervisor a COM-gated
`ControlHost` that drives the same `StateClient` and transition engine as the
old `nexigen-em` host, so `MachineFG` can now be implemented as an ordinary EM
Function Group with a few lifecycle rules.

`sm/state-machine` already records the deferred work: *“gating a step on an
explicit EM lifecycle report is deferred until the EM exposes a PlatformFG
report.”* This change removes that deferral when an EM is present.

## Goals / Non-Goals

**Goals:**

- Make `MachineFG` a reserved, validated EM Function Group.
- Let the EM own the initial `Off -> Startup` transition and expose its result.
- Reject an external `SetState(MachineFG, Off)`.
- Gate SM startup on the EM result and on `Verify`/`Run` confirmations.
- Preserve the standalone SM and manifests without `MachineFG`.

**Non-Goals:**

- ARXML `MachineManifest` (still JSON).
- UCM `Update`, Network Management, Diagnostics, or Suspend.
- A generic AUTOSAR state-machine interpreter (ActionLists/TransitionTables).
- Changing the existing application-Function-Group transition engine.

## Decisions

### D1 — Reserved `MachineFG` vocabulary lives in `nexigen_exec`

`include/nexigen/exec/machine_function_group.hpp` defines
`k_machine_function_group_name = "MachineFG"` and the six canonical state names.
`validate_suite_manifest` validates the group when present:

- the states are exactly `Off`, `Startup`, `Verify`, `Run`, `Shutdown`, and
  `Restart` (order-independent, but all six required);
- `initial_state` is empty, `Off`, or `Startup`; empty resolves to `Startup`;
- applications may bind to it like any other Function Group.

A manifest without `MachineFG` remains valid. `FunctionGroupRegistry` exposes
`is_machine_group(index)` and a name lookup so the supervisor and control host
do not string-compare in a hot loop.

### D2 — The EM owns the initial transition

`initial_state_for_group` returns `Startup` for `MachineFG` regardless of a
declared `initial_state` other than `Off`. The suite supervisor requests that
state through the existing pending-state path, so `Off -> Startup` runs through
the transition engine and confirms on readiness exactly like any other state.

`ComControlHost` records the first terminal result for `MachineFG`
(`Success`/`Off`/failure) in `initial_machine_state_result_`. `InProgress`
keeps the query unsettled. A later `Verify`/`Run` request never overwrites the
recorded initial result.

### D3 — `Off` is EM-owned

`StateClient::execute` rejects a command whose function group is `MachineFG`
and whose target is `Off` with `InvalidTransition`, before touching the
registry. The regression path is explicit: `ControlHost::shutdown_machine()`
calls `StateClient::shutdown_machine()`, which sets the registry state to `Off`
(and only when the state is declared) without going through `execute`. The
suite supervisor calls it on operator stop and on terminal suite failure.

### D4 — Query service

`include/nexigen/exec/machine_state_query.hpp` defines two trivially-copyable
payloads:

```cpp
struct InitialMachineStateTransitionResultRequest {};
struct InitialMachineStateTransitionResultResponse {
    StateResult result;
};
```

`ComControlHost` opens a request-response server on
`Nexigen/EM/InitialMachineStateTransitionResult` and serves it from `poll()`
before draining `StateClient`. The response returns the recorded result, or
`InvalidArgument` when the manifest declares no `MachineFG`. The request and
response are PODs, so no serialization or allocation occurs.

### D5 — SM lifecycle client

`include/nexigen/sm/machine_lifecycle.hpp` adds `sm::MachineLifecycleClient`
(COM-gated with `nexigen_sm`):

- `report_ready()` uses `exec::ApplicationStateReporter` for the SM's own app
  name, read from `--app-name` or `NEXIGEN_APP_NAME`.
- `initial_transition_result(timeout)` calls the query service.
- `request_state(target, timeout)` sends `StateCommand` on
  `Nexigen/EM/StateCommand` and retries while the ack is `InProgress`.

The SM startup sequence becomes:

1. `StateManager::begin_startup()` (SM view `Off -> Startup`).
2. `report_ready()`.
3. Wait up to the configured `startup_timeout_ms` for
   `initial_transition_result`.
4. On `Success`: `StateManager::advance_startup()`, request `MachineFG.Verify`,
   wait for `Success`, `StateManager::confirm_startup_step()`, request
   `MachineFG.Run`, wait for `Success`.
5. If the EM query is unavailable (`ConnectionFailure`/`Timeout`) the SM logs a
   warning and keeps the pre-change synchronous sequence. A terminal EM failure
   (`OperationFailed`/`UnexpectedTermination`) exits nonzero instead of
   pretending startup succeeded.

Shutdown sends a best-effort `MachineFG.Shutdown` (never `Off`), then performs
the existing SM-side shutdown sequence. The suite supervisor owns the eventual
`Off` through `shutdown_machine()`.

### D6 — Readiness reporting for platform processes

`ExecutionManager::start` already appends `--app-name <name>` to every child
argv but does not export `NEXIGEN_APP_NAME`. Add the environment variable next
to the other `NEXIGEN_*` values, and let `nexigen-sm` accept and ignore
`--app-name` while using the value for `ApplicationStateReporter`. This keeps
the child identity contract single-sourced and lets SM/PHM daemons participate
in `MachineFG` readiness.

## Risks / Trade-offs

- **SM kills itself during shutdown** → the SM only requests `Shutdown`, and a
  caller killed while the request is in flight is acceptable for shutdown. The
  supervisor still reaps it and then performs the EM-owned `Off`.
- **MachineFG absent** → the EM query returns `InvalidArgument`; the SM treats
  it as “no EM lifecycle integration” and keeps the synchronous fallback.
- **Extra startup latency** → one query plus two state confirmations, bounded
  by the existing `startup_timeout_ms`; no hot-path cost.
- **Duplicate readiness report** → `ApplicationStateReporter` is created once
  after transport setup; repeated reports are idempotent in the tracker.

## Migration Plan

1. Add the `MachineFG` constants, validation, and query payloads.
2. Add `Off` rejection and `shutdown_machine()`; record the initial result in
   `ComControlHost`.
3. Add the SM lifecycle client and gate `run_sm` startup; keep the fallback.
4. Add `NEXIGEN_APP_NAME` and `--app-name` handling for `nexigen-sm`.
5. Update manifests/examples and tests; run the four-config matrix.

## Open Questions

None blocking. MachineFG is optional for this change; making it mandatory in
every suite manifest is a later compatibility decision.
