# Proposal

## Why

`PlatformFG` currently lives only inside `nexigen::sm` as an enum plus a
synchronous `Off -> Startup -> Verify -> Run` sequence. The Execution Manager
has no `MachineFG` Function Group, so:

- no manifest can bind platform-foundation processes to `MachineFG` states;
- the SM cannot ask the EM whether its initial machine-state transition
  succeeded (`GetInitialMachineStateTransitionResult`);
- an external `SetState(MachineFG, Off)` would be accepted even though `Off` is
  an EM-owned state;
- startup already emits “an explicit EM lifecycle report gate is future
  integration”, so the SM advances `Verify -> Run` without any execution-side
  confirmation.

This change makes `MachineFG` a first-class EM Function Group and closes the
SM/EM lifecycle loop on top of the control host introduced by
`unify-exec-suite-control-path`.

## What Changes

- Reserve the Function Group name `MachineFG` with the fixed states `Off`,
  `Startup`, `Verify`, `Run`, `Shutdown`, and `Restart`; validate it in the
  suite manifest when declared.
- Have the EM self-initiate the `Off -> Startup` transition at suite start and
  record the terminal result of that initial machine transition.
- Reject an external `SetState(MachineFG, Off)` with `InvalidTransition`;
  `Off` remains reachable only through the EM's own shutdown path.
- Add a `GetInitialMachineStateTransitionResult` request-response service so a
  State Manager can read that result.
- Gate the SM startup sequence on the EM: report SM readiness, query the
  initial machine transition result, then command `MachineFG` `Verify` and
  `Run` through `StateClient` and wait for `Success`. When no EM control host is
  present, keep the current synchronous fallback so standalone SM runs and
  existing tests continue to work.

## Capabilities

### New Capabilities

- `exec/machine-function-group`: the reserved `MachineFG` vocabulary, manifest
  validation, EM-owned initial transition, `Off` rejection, and the
  `GetInitialMachineStateTransitionResult` query.

### Modified Capabilities

- `exec/app-suite`: `MachineFG` is a reserved Function Group with a fixed state
  vocabulary and an EM-selected initial `Startup` state.
- `exec/state-client`: external `MachineFG.Off` requests are rejected; the
  initial machine transition result is observable.
- `sm/state-machine`: startup and shutdown sequencing is gated on EM lifecycle
  reports instead of advancing synchronously when an EM is available.

## Impact

- **Performance:** cold path only. One extra request-response at SM startup and
  one per machine-state step; no hot-path allocation, locking, or record-format
  change.
- **Compatibility:** manifests without `MachineFG` keep the existing behavior.
  A standalone SM without an EM keeps the documented synchronous fallback.
- **Dependencies:** reuses the COM-gated `ControlHost`/`StateClient` path; no
  COM symbol enters `nexigen_exec`.
