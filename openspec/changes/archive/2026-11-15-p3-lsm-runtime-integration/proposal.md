# Proposal

## Why

`SmManifest::state_machines` is parsed and validated, but the arbitration loop
never executes it. Declarative transition tables, action lists, and error
recovery are therefore dead configuration: a TriggerIn still goes straight to
`StateManager::request_transition`.

## What Changes

- Bind each `LightweightStateMachineConfig` to one Function Group through a new
  `function_group` field.
- Add `sm::LsmRuntime` and an `EmActionSink`:
  - `RequestFunctionGroupState` sends a `StateCommand` and waits for the EM;
  - `PublishState` writes the existing StatePublication field;
  - successful transitions synchronize the `StateManager` state.
- Route TriggerIn for a bound group through the LSM; unbound groups keep the
  existing `StateManager` path. UCM locks still take precedence.
- Add `TransitionResult::ActionFailed` for an LSM action that fails after
  recovery.
- Enforce the application-group/PlatformFG gate and validate that every LSM
  state is declared by its Function Group.

## Capabilities

### New Capabilities

- `sm/lsm-runtime`: Function Group binding, the EM action sink, TriggerIn
  routing, and state synchronization.

### Modified Capabilities

- `sm/lightweight-state-machine`: configuration gains `function_group`.
- `sm/manifest`: validation checks LSM bindings and state subsets.
- `sm/state-machine`: the result vocabulary gains `ActionFailed`.
- `sm/ipc`: TriggerIn for a bound group is served by the LSM.

## Impact

- **Performance:** cold control path only. LSM lookup is a small vector scan;
  actions are the same EM request-response and Field publish already used by the
  SM.
- **Compatibility:** manifests without `state_machines` keep the existing
  behavior. A bound group now uses its declarative table instead of the generic
  graph, which is the point of the feature.
- **Safety:** UCM locks and the PlatformFG application-group gate still apply.
