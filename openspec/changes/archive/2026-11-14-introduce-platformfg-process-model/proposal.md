# Proposal

## Why

The platform's state and process model is a simplification of the AUTOSAR Adaptive Platform it tracks. Concretely: the `MachineState` machine carries only `Startup`/`Verify`/`NormalRunning`/`Shutdown` (missing the mandatory `Off` and `Restart`), processes are not typed by termination behavior (a one-shot initialization task and a long-lived daemon are treated the same), there is no `Unrecoverable State` for emergencies, and Function Group state changes are confirmed without a readiness condition. A HIL integration bench needs exactly the missing pieces: a `PlatformFG` foundation (broker, SM, PHM, EM, DataRecorder) that must reach `Run` before any application group starts, and a clear daemon/oneshot distinction so a daemon dying during a simulation is an emergency while a one-shot task exiting 0 is normal.

## What Changes

- Rename and extend the machine Function Group to `PlatformFG` with states `Off`, `Startup`, `Verify`, `Run`, `Shutdown`, `Restart` (`Run` replaces `NormalRunning` as the target working state; `Off` is the mandatory initial state with no bound processes; `Shutdown` is the transition into `Off`; `Restart` is a fast reboot without full power-off). `Diagnostics` is deferred and recorded in the design.
- Introduce a process execution type on the process manifest: `daemon` (continuous, must report `Run` readiness, unexpected self-termination is a failure) versus `oneshot` (self-terminating, exit 0 is success), mirroring AUTOSAR `terminationBehavior` (`processIsNotSelfTerminating` / `processIsSelfTerminating`) and the Reporting/Non-reporting distinction.
- Add the `Unrecoverable State` the Execution Manager enters on an unresolvable failure (e.g. a daemon terminating during an active phase), where it stops acting and terminates all managed processes.
- Define the state-transition confirmation condition: a Function Group switch to `Run` is confirmed only when every bound `daemon` has reported readiness and every bound `oneshot` has terminated successfully.

## Capabilities

### New Capabilities

- `exec/process-types`: the process execution model — `daemon`/`oneshot` termination behavior, readiness reporting, the `Unrecoverable State`, and the state-transition confirmation condition.

### Modified Capabilities

- `sm/state-machine`: the single machine state machine becomes the `PlatformFG` Function Group with the `Off`/`Startup`/`Verify`/`Run`/`Shutdown`/`Restart` state vocabulary (`Run` replaces `NormalRunning`).

## Impact

- **Code**: `include/nexigen/sm/machine_state.hpp` (state vocabulary), the `sm` state manager sequencing, `include/nexigen/exec/` process-supervision and app-suite types, `src/exec/*` supervision loop, and the process/suite manifest schema.
- **Performance / latency / allocation**: no producer or worker hot path changes; the new logic is cold-path supervision and state-change control. The confirmation condition adds no allocation (it reuses the existing per-process state bookkeeping), and `Unrecoverable State` is a control-plane path reached only on failure.
- **Breaking changes**: `MachineState` is renamed to `PlatformFG` and `NormalRunning` to `Run`; manifest and API consumers of the machine state must migrate.
