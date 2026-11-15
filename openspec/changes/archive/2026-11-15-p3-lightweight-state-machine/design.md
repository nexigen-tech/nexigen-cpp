# Design

## Context

`StateManager` owns the existing graph/priority arbitration. The lightweight
state machine is a declarative control layer for a single group; it must not
replace the arbiter.

## Goals / Non-Goals

**Goals:**

- Action-list execution after an accepted transition.
- ErrorRecoveryTable fallback.
- JSON configuration and an injectable action sink for tests/EM wiring.

**Non-Goals:**

- Running an LSM from the arbitration loop (follow-up).
- A full AUTOSAR StateMachine metamodel (nested states, guards, expressions).
- Dynamic loading, scripting, or a new daemon.

## Decisions

### D1 — Action sink boundary

```cpp
class LsmActionSink {
public:
    virtual ~LsmActionSink() = default;
    virtual core::Status request_state(std::string_view group, std::string_view state,
                                       std::string_view requester) = 0;
    virtual core::Status publish_state(std::string_view group, std::string_view state) = 0;
};
```

`RequestFunctionGroupState` calls `request_state`; `PublishState` calls
`publish_state`. `None` is a no-op. The sink keeps the interpreter free of COM
and lets tests record exact action order.

### D2 — Transition rule

`LsmTransition { from, to, actions }` is matched state-name-exact. `request`
rejects an unknown target or a missing edge with `InvalidGraph`. On success it
sets the state only after every action succeeds. On failure it applies the
error-recovery table: the first rule whose `from` matches the failed target (or
the current state) publishes/requests its fallback and runs its actions, then
the machine state becomes the fallback. No rule leaves the state unchanged and
returns the action error.

### D3 — JSON shape

```json
{
  "name": "adas_lsm",
  "states": ["Off", "Active", "Fallback"],
  "initial_state": "Off",
  "actions": [
    {"type": "RequestFunctionGroupState", "function_group": "adas", "target_state": "Active"}
  ],
  "transitions": [{"from": "Off", "to": "Active", "actions": [0]}],
  "error_recovery": [{"from": "Active", "fallback_state": "Fallback", "actions": [1]}]
}
```

Action indices are validated against the action list.

### D4 — Manifest integration

`SmManifest::state_machines` is an optional vector of these configs. The
arbitration loop does not consume them yet; the manifest is the declarative
source the next integration step will activate. This keeps the change
behavior-neutral for existing deployments.

### D5 — No concurrency

The machine is single-threaded by contract and carries no atomics. One
instance belongs to one control/arbitration thread.

## Risks / Trade-offs

- **Not wired into arbitration yet** → the engine is tested independently and
  loadable from the manifest; wiring can land separately without changing the
  configuration format.
- **Action indices are brittle** → validation rejects out-of-range indices.
- **No expressions/guards** → only static tables are supported; guards remain a
  future extension.

## Migration Plan

1. Add the vocabulary/interpreter and JSON.
2. Add `state_machines` to `SmManifest`.
3. Add sink-driven tests and manifest round-trip tests.
4. Document the configuration shape and run the matrix.

## Open Questions

None blocking. Whether the SM should select an LSM per Function Group or run
several is deferred.
