# Design

## Context

See `proposal.md` — Why. The platform tracks the AUTOSAR Adaptive Platform but simplifies it. Today the `sm` module has a fixed `MachineState` enum (`Startup`/`Verify`/`NormalRunning`/`Shutdown`, no `Off`/`Restart`), processes are not typed by termination behavior, there is no `Unrecoverable State`, and Function Group state changes are confirmed without a readiness condition. This change aligns the model with AUTOSAR (SWS_ExecutionManagement R24-11): the machine is a Function Group, processes declare `terminationBehavior`, and state changes are gated on readiness.

## Goals / Non-Goals

**Goals:**
- Introduce `PlatformFG`, the platform-foundation Function Group, with states `Off`/`Startup`/`Verify`/`Run`/`Shutdown`/`Restart` (`Run` replaces `NormalRunning`).
- Introduce the process execution type (`daemon`/`oneshot`) and readiness reporting.
- Add the `Unrecoverable State` and the state-transition confirmation condition.

**Non-Goals:**
- No application Function Group semantics beyond `PlatformFG` (application groups are a follow-up).
- `Diagnostics` state — deferred (recorded here for later).
- No `DataRecorder` implementation — it is a future `PlatformFG` service, named only.

## Decisions

### D1 — PlatformFG is a Function Group, not a separate enum

AUTOSAR models the machine as a Function Group named `MachineFG` ([SWS_EM_CONSTR_02556]). This change renames the `MachineState` enum to a `PlatformFG` Function Group with a clear semantic: it owns the platform foundation (message broker over iceoryx2, SM, PHM, EM, and the future DataRecorder). Application groups are separate and cannot start until `PlatformFG` reaches `Run`.

### D2 — State vocabulary

`Off`, `Startup`, `Verify`, `Run`, `Shutdown`, `Restart`. `Off` is the mandatory initial state with no bound processes (AUTOSAR `Off` is mandatory in every group, [SWS_EM_01110]); `Run` is the target working state (machine-specific, renamed from `NormalRunning` for consistency with the verb-style names); `Shutdown` is the transition into `Off`; `Restart` is a fast reboot without full power-off.

### D3 — Startup vs Verify

`Startup` brings the platform processes up (EM self-initiates the transition); `Verify` runs the foundation self-diagnosis (broker, SM, PHM, EM each report healthy) before `Run`. They are distinct so a half-broken foundation can never reach `Run`. The host advances these steps synchronously; gating a step on an explicit EM lifecycle report is deferred until the EM exposes a PlatformFG report (the EM currently reports application readiness, not platform phases).

### D4 — Process termination behavior

The process manifest gains `type: daemon` (continuous) or `type: oneshot` (transient), mirroring AUTOSAR `terminationBehavior` (`processIsNotSelfTerminating` / `processIsSelfTerminating`). A `daemon` runs for the whole duration of its bound phases and must report `Run`; a `oneshot` self-terminates and exit 0 is success.

### D5 — Readiness reporting

A `daemon` reports readiness via `ReportExecutionState` (Reporting process, [SWS_EM_01004]); a `oneshot` need not report (Non-reporting, [SWS_EM_01402]). A `daemon` terminating before reporting is an unexpected self-termination. The `nexigen-em` host instantiates the `ExecutionStateTracker`; `StateClient` opens one `Nexigen/EM/State/<app>` listener per bound application, drains it into the tracker on every `serve_once`, and polls `exit.json` for pending oneshots. Confirmation is therefore fed by the real state channel, not by a caller-maintained table.

### D6 — Unexpected self-termination and Unrecoverable State

A `daemon` terminating without a stop request, or before readiness, or any process exiting non-zero, is a failure. On an unresolvable failure the EM enters the `Unrecoverable State` ([SWS_EM_02555] / the `Unrecoverable State` term): stop acting, terminate all managed processes, invoke project-specific handling (e.g. emergency power-off). The suite owner holds one latch for its lifetime, so a repeated failure path cannot re-run the terminator or hook.

### D7 — State-transition confirmation

A Function Group switch to `Run` is confirmed only when every bound `daemon` has reported readiness and every bound `oneshot` has terminated successfully ([SWS_EM_01067] Confirm State Changes). An unready daemon within its startup timeout blocks confirmation. A manifest is rejected when an application group does not declare `Off` or does not start in `Off`/`SafeState`; `StateManager::create()` is the only construction path (the raw constructor is private) and validates before construction, so the first declared state cannot bypass the startup gate. A retried switch skips processes that are already ready, failed, exited, or still awaiting oneshot completion, so retries cannot launch a second instance.

### D8 — Diagnostics deferred

`Diagnostics` is a machine-specific state for a later change; it belongs to `PlatformFG` alongside `Run` when the bench needs a service/diagnostic mode.

## Risks / Trade-offs

- [Rename is breaking] → `MachineState` → `PlatformFG` and `NormalRunning` → `Run` break API/manifest consumers; the change migrates them and the tasks call out the affected sites.
- [Confirmation gating adds latency to state changes] → it is cold-path control-plane work only; the producer/worker hot paths are untouched.
- [Self-termination detection depends on wait status] → the EM already observes exit status via `waitpid` ([SWS_EM_01006]); the change only classifies it by `terminationBehavior`.

## Open Questions

None — the state set, the rename, and the process model are decided above.
