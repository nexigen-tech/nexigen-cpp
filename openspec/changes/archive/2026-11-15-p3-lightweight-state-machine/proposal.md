# Proposal

## Why

Function Group state logic is currently hand-coded: allowed transitions live in
`FunctionGroup`, while actions and failure handling are scattered across the SM
main loop. AUTOSAR's lightweight StateMachine vocabulary (TransitionTable,
ActionList, ErrorRecoveryTable) is missing, so a deployment cannot describe
"on `Off -> Active`, request these states; on failure, recover like this"
declaratively.

## What Changes

- Add `LsmActionType` (`None`, `RequestFunctionGroupState`, `PublishState`),
  `LsmAction`, `LsmTransition`, `LsmErrorRecovery`, and
  `LightweightStateMachineConfig`.
- Add `sm::LightweightStateMachine`, an interpreter with an `LsmActionSink`
  boundary: it validates the transition table, executes the action list in
  order, and applies the error-recovery table when an action fails.
- Provide JSON round-trip and diagnostics for every new type.
- Add an optional `state_machines` array to `SmManifest` so the SM can load LSM
  configurations beside its existing groups. Runtime wiring to the arbitration
  loop remains a follow-up; this change lands the declarative engine and its
  tests.

## Capabilities

### New Capabilities

- `sm/lightweight-state-machine`: declarative transition tables, action lists,
  error recovery, and the action sink boundary.

### Modified Capabilities

- `sm/manifest`: `SmManifest` accepts an optional `state_machines` array and
  round-trips it.

## Impact

- **Performance:** cold-path configuration and explicit requests only; no
  per-record hot-path work. The interpreter is O(transitions + actions) for a
  small in-memory table and allocates only during construction/JSON.
- **Compatibility:** manifests without `state_machines` are unchanged; the
  existing `StateManager` remains the runtime arbiter.
- **Dependencies:** pure `nexigen::sm`; no COM or XML.
