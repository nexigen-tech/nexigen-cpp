# Proposal

## Why

`align-exec-process-model-with-autosar` introduced the state-dependent process
model, process table, transition engine, and `StateClient` result protocol in
the `nexigen-em` host. The suite supervisor (`nexigen-exec start --config`)
still runs its legacy launch loop: it marks a launched process `Running`
without waiting for `ReportExecutionState`, and does not host `StateClient`.
Readiness-gated dependency release and transition confirmation therefore differ
between the two EM paths. Unifying them removes that split and lets the suite
path use the same readiness inbox, process table, and transition engine.

## What Changes

- Add a COM-gated `nexigen_exec_control` component that owns the `ProcessTable`,
  `FunctionGroupRegistry`, `FunctionGroupTransition`, readiness inbox, and
  `StateClient`.
- Make the forked suite supervisor use that control component on its tick:
  launch remains asynchronous, but a reporting process stays `Starting` until
  its readiness event arrives; dependency release and transition confirmation
  use the process table.
- Fold `nexigen-em` into the same control loop (thin front-end), removing its
  separate direct per-app `ExecutionManager::start` path.
- Drive `FunctionGroupConfig.initial_state` through the transition engine rather
  than the current direct process selection.
- Preserve the gate-OFF build: `nexigen_exec` stays COM-free; the control
  component is linked only when `NEXIGEN_ENABLE_COM=ON`. With the gate off,
  reporting processes are unsupported and the file-based/non-reporting fallback
  is documented.
- **BREAKING**: `nexigen-exec start --config` keeps its auto-start contract but
  transitions now wait for readiness; a daemon that never reports can block the
  suite until the startup timeout/failure path.

**Out of scope:** `PlatformFG` as an EM Function Group, EM→PHM lifecycle
notifications, Resource Groups, Trusted Platform, Deterministic Execution,
ARXML manifests, UCM, Network Management, Diagnostics, lightweight StateMachine,
and Suspend-to-RAM.

## Capabilities

### New Capabilities

- None. The control-path behavior belongs to the existing EM capabilities.

### Modified Capabilities

- `exec/process-supervision`: readiness-gated launch and dependency release in
  the suite supervisor, using the shared process table and control component.
- `exec/state-client`: the suite supervisor hosts the `StateClient` receiver
  and exposes the same transition result protocol as `nexigen-em`.

## Impact

- **Code:** `src/exec/suite_supervisor.cpp`, `src/exec/suite_control.cpp`,
  `src/exec/state_client.cpp`, new `nexigen_exec_control` target/source,
  `tools/nexigen-exec`, `tools/nexigen-em`, CMake, `tests/exec/`.
- **APIs:** no public vocabulary change beyond the existing `StateResult`
  protocol; `nexigen-exec start --config` timing changes because readiness is
  now a gate.
- **Performance / latency:** control-plane cold path only. No producer, worker,
  harvest, or sink hot path changes; no data-plane allocation or latency change.
  The supervisor tick gains a readiness-inbox drain and process-table checks,
  bounded by the manifest size.
- **Memory:** one process-table record per application and one readiness inbox
  per bound application, both pre-sized from the manifest. COM listeners add
  the existing transport handle cost only when the gate is enabled.
- **Build:** a new COM-gated `nexigen_exec_control` target; gate-OFF builds keep
  the current suite supervisor behavior with the documented reporting
  limitation.
