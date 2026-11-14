# Proposal

## Why

`introduce-platformfg-process-model` aligned the state vocabulary, daemon/oneshot
termination behavior, readiness confirmation, and Unrecoverable State with the
AUTOSAR AP model. An audit against `AUTOSAR_AP_SWS_ExecutionManagement` R24-11
(§7.4–7.5), `AUTOSAR_AP_SWS_StateManagement` R24-11, and the manifest
specification shows that the EM still treats Function Group membership as a flat
group-to-process list, marks a launched process `Running` before it reports
readiness, and has no transition object, `Undefined` state, or transition
timeouts. For a HIL bench with multi-state application groups and legacy
non-reporting daemons this selects the wrong process set, defeats dependency
gating, and makes failed switches indistinguishable from “not ready yet”.

## What Changes

- **State-dependent process mapping.** A process binds to specific Function
  Group States (`StateDependentStartupConfig`), not to a whole Function Group.
  A process belongs to at most one Function Group; `Off` maps no processes. A
  state transition terminates processes not needed in the requested state,
  restarts processes whose startup configuration changed, and starts the newly
  required processes.
- **Readiness-aware supervision.** The suite supervisor feeds the same readiness
  tracker/listener path as the `StateClient`, so dependency gating waits for
  `ReportExecutionState(kRunning)`, not merely for process creation. Reporting
  and non-reporting processes become orthogonal to `daemon`/`oneshot`:
  non-reporting processes enter `Running` implicitly, while a non-reporting
  process that attempts to report is a violation.
- **Suite startup enters a configured initial state.** A Function Group may
  declare an `initial_state` (default: its first non-`Off` state). The suite
  supervisor still starts in the manifest’s `Off` initial state, then starts the
  processes configured for each group’s initial state, so
  `nexigen-exec start --config` keeps its auto-start contract without mapping
  processes to `Off`. Moving this selection behind the unified transition engine
  is part of the follow-up control-path change.
- **Process state model.** Expose the AUTOSAR process lifecycle internally
  (`Idle`, `Starting`, `Running`, `Terminating`, `Terminated`) alongside the
  existing execution-state reporting, so dependency resolution, timeouts, and
  PHM-facing lifecycle events have a precise state to observe.
- **Execution dependencies.** Dependencies carry the required process state
  (`Running` or `Terminated`), are scoped to one Function Group State, reject
  mutual dependencies and cross-state references, and impose reverse order on
  termination. Per-process startup/termination timeouts and restart attempts
  apply during a transition.
- **Transition lifecycle and `Undefined` state.** `StateClient` tracks one
  transition per Function Group, supports cancellation/replacement of an
  in-flight transition, monitors startup and termination timeouts, and on
  failure sets the group to `Undefined` and reports the AUTOSAR-style failure
  result (`kOperationFailed`, `kUnexpectedTermination`, `kOperationCanceled`)
  instead of only rejecting the command.
- **BREAKING**: the suite manifest gains per-state startup configurations
  (`function_group_states`, arguments, environment, timeouts, restart attempts);
  `AppSpec.function_groups` becomes a single Function Group reference; the
  `StateClient` acknowledgement grows transition result/error fields.

**Out of scope for this change:** moving `StateClient`, readiness listeners, and
the transition engine into the suite supervisor child (COM-gated
`nexigen_exec_control`) and folding `nexigen-em` into that loop — deferred to the
follow-up `unify-exec-suite-control-path` change. Also deferred: making
`PlatformFG` a first-class EM Function Group with
`GetInitialMachineStateTransitionResult`/`MachineFG.Off` handling; EM→PHM
lifecycle notifications; Resource Groups; Trusted Platform/authenticated
startup; Deterministic Execution; ARXML manifests; UCM, Network Management,
Diagnostics, lightweight StateMachine, and Suspend-to-RAM.

## Capabilities

### New Capabilities

- `exec/function-group-state`: state-dependent process sets, the
  terminate/restart/start transition algorithm, `Undefined` state, transition
  timeouts, and transition result semantics.

### Modified Capabilities

- `exec/app-suite`: manifest schema for per-state startup configurations and the
  one-Function-Group-per-process / no-`Off`-mapping constraints.
- `exec/state-client`: one in-flight transition per Function Group,
  cancellation/replacement, and AUTOSAR-style transition result/error reporting.
- `exec/process-supervision`: process lifecycle states, readiness-aware
  dependency gating, `Running`/`Terminated` dependencies, reverse termination
  order, cycle validation, and per-state timeout/restart handling.
- `exec/process-types`: reporting versus non-reporting processes, independent of
  the `daemon`/`oneshot` termination behavior.

## Impact

- **Code:** `include/nexigen/exec/` (`app_spec`, `function_group_registry`,
  `execution_state_tracker`, `state_client`, `application_state`, new
  transition/state types), `src/exec/` (`suite_supervisor`, `state_client`,
  `function_group_registry`, `suite_manifest`, `child_process`), tools
  `nexigen-exec`/`nexigen-em`, and `tests/exec/`.
- **Manifests (BREAKING):** per-state startup configurations; one Function Group
  per application; explicit reporting mode; `Off` cannot reference a process.
- **APIs (BREAKING):** `StateClient` acknowledgement/result semantics and the
  transition state observation surface.
- **Performance / latency:** control-plane cold path only. No producer, worker,
  harvest, or sink hot path changes; no allocation on data-plane loops. Transition
  tables and per-process state records are pre-sized from the manifest. State
  switches gain the intended per-process readiness/timeout waiting; data-plane
  latency is unchanged.
- **Memory:** one small transition record per Function Group and one process-state
  record per process, both bounded by manifest size. JSON and `std::string` remain
  configuration/diagnostic cold-path only.
- **Tests:** new transition/Undefined/timeout scenarios, readiness-gated
  dependency tests, non-reporting daemon tests, one-Function-Group and no-`Off`
  validation tests, plus the existing suite/sm/exec matrix.
