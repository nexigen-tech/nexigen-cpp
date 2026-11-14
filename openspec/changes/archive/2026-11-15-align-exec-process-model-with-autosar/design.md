# Design

## Context

See `proposal.md` — Why. The current tree has two execution paths: the suite
supervisor (`nexigen-exec start --config` → `ExecutionManager::start_suite` →
`src/exec/suite_supervisor.cpp`) and the readiness/StateClient host
(`nexigen-em` → `src/exec/state_client.cpp`). `FunctionGroupRegistry` maps a
Function Group to a flat application list; `AppSpec.function_groups` is a vector;
`ChildProcess::launch` sets `AppState::Running` immediately; the suite supervisor
never listens to `Nexigen/EM/State/<app>`. `ExecutionStateTracker` is only used
by `StateClient`. `AppSpec.dependencies` is a flat Running-only list, and
`max_dependency_startup_ns` uses `idle_timeout_s`.

## Goals / Non-Goals

**Goals:**

- Make Function Group State the unit that selects processes, with the AUTOSAR
  terminate/restart/start transition algorithm.
- Give the EM a precise process lifecycle and one readiness-aware control path.
- Make dependencies state-qualified (`Running`/`Terminated`) and order both
  startup and termination.
- Report transition progress, cancellation, and failures (`Undefined`) instead
  of only accepting/rejecting a command.

**Non-Goals:**

- `PlatformFG` as a first-class EM Function Group, initial-machine-state result
  API, and `MachineFG.Off` handling.
- EM→PHM lifecycle notifications.
- Resource Groups, Trusted Platform, Deterministic Execution, ARXML manifests,
  UCM, Network Management, Diagnostics, lightweight StateMachine, Suspend-to-RAM.

## Decisions

### D1 — Manifest: one Function Group and per-state startup configurations

`AppSpec.function_groups` is replaced by one `function_group` reference plus a
non-empty `startup_configs` list. Each `StateDependentStartupConfig` names a
state in that group and carries `args`, `env`, `startup_timeout_ms`,
`termination_timeout_ms`, `restart_attempts`, and `restart_delay_s`. A process
with no configuration for the requested state is not started; a configuration
that references `Off`, a missing state, or a second Function Group is rejected
at manifest validation. This mirrors the manifest meta-model and makes the
state→process table derivable at load time. Alternative considered: keep
group-level bindings and add a state filter at runtime — rejected because it
cannot express different arguments/timeouts per state and leaves the process
set ambiguous.

**BREAKING:** existing manifests that use `function_groups` or app-level
`args`/`env`/`stop_grace_s` must migrate to `startup_configs`.

### D2 — State→process table and transition engine

`FunctionGroupRegistry` builds `state -> [process]` from the validated
`startup_configs`. A new cold-path `FunctionGroupTransition` owns one
CurrentState/RequestedState pair per Function Group and advances through three
phases: terminate processes not needed in RequestedState, restart processes
whose startup configuration differs, start processes needed in RequestedState.
Dependency order is computed per phase (topological startup order, reverse
termination order). The transition is non-blocking and advances on the
supervision tick; it stores per-process target state, deadline, and restart
budget. Only one transition per group is active; a new validated request
cancels the old one and reports `OperationCanceled` on its result. Alternative
considered: synchronous start/stop in `StateClient` — rejected because a
blocking transition cannot report progress or be cancelled.

### D3 — Process table and lifecycle states

A cold-path `ProcessTable` replaces the tracker’s readiness-only records. It
stores, per process: declared termination behavior (`daemon`/`oneshot`),
reporting mode, `ProcessState` (`Idle`, `Starting`, `Running`, `Terminating`,
`Terminated`), the startup configuration, and the current attempt. Reporting
mode is independent of termination behavior: a non-reporting process enters
`Running` immediately after creation; a reporting process enters `Running` only
after `ReportExecutionState(kRunning)`. A report from a non-reporting process
is rejected and logged. `AppState` remains the suite/status JSON vocabulary;
the EM maps `ProcessState` to `AppState` at the status boundary so existing
`suite-state.json` consumers keep their enum.

### D4 — Control host in this change

`nexigen-em`/`StateClient` remains the COM/state control host for this change.
It owns the `ProcessTable`, `FunctionGroupRegistry`, transition engine,
`Nexigen/EM/State/<app>` listeners, and the request/response channels. The suite
supervisor keeps its existing launch loop and selects the processes configured
for each group’s `initial_state`; it does not host `StateClient` in this change.
Moving the control path into the suite supervisor child (a COM-gated
`nexigen_exec_control` target) and folding `nexigen-em` into that loop is the
follow-up `unify-exec-suite-control-path` change. This keeps `nexigen_exec`
COM-free and avoids rewriting the forked supervisor topology inside the process
model change.

### D5 — StateClient protocol and transition results

`StateAck` keeps `accepted` for wire compatibility and gains a `result`
vocabulary: `Success`, `InProgress`, `OperationFailed`, `OperationCanceled`,
`UnexpectedTermination`, `InvalidTransition`, `InvalidArgument`. `SetState`
returns `InProgress` while the transition is running; the SM retries on
`InProgress` and treats the terminal results as failures. On a start/termination
timeout or unexpected termination of a requested-state process, the transition
stops, the group state becomes `Undefined`, and the result identifies the
failure. On success the RequestedState becomes CurrentState. `StateClient`
validates the caller’s group and state before cancelling an in-flight
transition. Because each command has one acknowledgement, a replaced transition
is observed through the new request’s result; `OperationCanceled` remains
reserved for a future request-id/async-ack protocol.

### D6 — Execution dependencies

`StateDependentStartupConfig` carries `ExecutionDependency` entries: target
process and required state, restricted to `Running` or `Terminated`. Dependencies
must reference a process in the same Function Group and the same RequestedState;
mutual dependencies and cycles are rejected by validation. Startup launches a
process only after every dependency reaches its required state during the
current transition. Termination runs in reverse dependency order; a process
that self-terminated already is not an error. A dependency that is not part of
the RequestedState is a configuration error and fails the dependent’s startup.

### D7 — Timeouts and failure handling

Each startup configuration supplies startup and termination timeouts and a
restart budget. A termination timeout sends SIGKILL and continues the
transition (the target state must remain reachable). A startup timeout kills the
attempt, retries up to the configured attempts, and then stops the transition,
sets `Undefined`, and reports `OperationFailed`. Unexpected termination of a
requested-state process stops the transition and reports
`UnexpectedTermination`; unexpected termination of a process that is only being
stopped is logged and does not abort the transition. All waits use the existing
monotonic clock and supervision tick; no new hot-path timer or allocation is
introduced.

### D8 — Compatibility boundaries

`AppState` and `suite-state.json` remain the status contract; the new
`ProcessState`/`StateResult` enums get the full `to_string`/`from_string`,
`to_json`/`from_json`, and `std::formatter` set for diagnostics and tests.
`ExecutionStateTracker`’s public methods are either extended or replaced by
`ProcessTable` with a compatibility wrapper during migration. No producer,
worker, harvest, or sink hot path is touched; all new state is cold-path
control-plane only.

### D9 — Suite initial state

`FunctionGroupConfig` gains an optional `initial_state`. The manifest still
starts every group in `Off`; at suite start the supervisor selects the processes
configured for each group’s `initial_state` and launches them. When omitted, the
default is the first non-`Off` state, so existing suites keep their auto-start
behavior with one manifest field. If a group has only `Off`, the suite stays
idle until an explicit `SetState`. Replacing this direct selection with the
unified transition engine is part of the follow-up control-path change.
Alternative considered: auto-launch every process at suite start — rejected
because it contradicts the `Off`-initial model; strict “do nothing until
`SetState`” — rejected because it breaks the `nexigen-exec start --config`
contract and every existing example.

## Risks / Trade-offs

- **[Breaking manifest and ack changes]** → migrate examples, tests, and the SM
  client in the same change; keep `accepted` for one release where possible and
  document the new `result` field.
- **[COM-enabled supervisor build complexity]** → isolate it in
  `nexigen_exec_control`; keep `nexigen_exec` COM-free and keep the gate-OFF
  build green with the file-based fallback.
- **[Transition deadlocks from bad dependencies]** → validate cycles and
  mutual dependencies at load time; fail a missing RequestedState dependency
  visibly rather than waiting forever.
- **[Timeout tuning in HIL]** → make timeouts per startup configuration, log
  every timeout, and keep the existing stop-grace/SIGKILL behavior.
- **[Behavior change for existing multi-group manifests]** → reject them at
  validation with a migration message instead of silently changing which
  processes run.

## Migration Plan

1. Add `ProcessState`, `StateResult`, `StateDependentStartupConfig`, and
   `ExecutionDependency` types with serialization and validation; migrate
   manifests/examples to one Function Group and per-state configs.
2. Build the `ProcessTable` in the `nexigen-em`/`StateClient` host and feed it
   from the existing `Nexigen/EM/State/<app>` listeners; keep the suite
   supervisor on its existing launch loop with `initial_state` selection. The
   COM-gated supervisor `ExecutionControl` and `nexigen-em` fold-in are the
   follow-up `unify-exec-suite-control-path` change.
3. Implement `FunctionGroupTransition` in the `StateClient` host
   (stop/restart/start, dependency order, timeouts, cancellation, `Undefined`)
   and the `StateAck::result` protocol; update `nexigen-sm` to retry
   `InProgress` and surface terminal failures.
4. Update status/JSON mapping, tests, docs, and the full build matrix; remove
   the old `AppState`-based readiness checks once the new path is green.
5. Rollback = revert this change alone; manifests are migrated by the same
   commit, and `AppState`/`suite-state.json` stay compatible.

## Open Questions

None blocking. Suite-supervisor unification and the gate-OFF reporting contract
belong to the follow-up `unify-exec-suite-control-path` change.
