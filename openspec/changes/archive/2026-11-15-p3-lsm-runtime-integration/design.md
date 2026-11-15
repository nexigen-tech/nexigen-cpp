# Design

## Context

`LightweightStateMachine` already interprets transition/action/recovery tables
against an `LsmActionSink`. The SM main loop owns the EM command client, the
StatePublication, the StateManager, and the TriggerIn server. Runtime
integration is the adapter between those pieces.

## Goals / Non-Goals

- **Goals:** bind an LSM to a group, execute it from TriggerIn, keep EM/state
  publication in sync, surface failures as `ActionFailed`.
- **Non-Goals:** multiple LSMs per group, guards/expressions, dynamic
  reconfiguration, replacing the generic graph for unbound groups.

## Decisions

### D1 — Explicit group binding

`LightweightStateMachineConfig` gains `function_group`. Manifest validation
requires a non-empty name, exactly one LSM per group, a declared group, and
every LSM state present in that group's state vocabulary. A manifest with no
`state_machines` keeps the old behavior.

### D2 — `LsmRuntime` and `EmActionSink`

`LsmRuntime` owns one `EmActionSink` and one `LightweightStateMachine` per
bound group. The sink:

- `request_state(group, state, requester)` builds a `StateCommand`, carries the
  requester id and a monotonically increasing request id, and retries
  `InProgress` up to the group's `transition_max_time_ms`. `Success` updates the
  `StateManager` state through `force_state`.
- `publish_state(group, state)` writes the existing `StatePublication` Field.
- Com failures map to `core::ErrorCode::SystemError`.

The runtime initializes each machine at the group's current state; validation
guarantees that state is in the LSM vocabulary.

### D3 — TriggerIn routing

`serve_one_request` resolves the group and checks the UCM lock first. If a
runtime binding exists, it calls `lsm->request(index, target, requester)`;
otherwise it keeps `request_with_update_lock`/`StateManager`. After an LSM
attempt, the runtime synchronizes the `StateManager` to the machine's current
state, including a recovery fallback.

A failed action returns `ActionFailed` after recovery. Missing transitions still
return `InvalidGraph`. The application-group gate is enforced before invoking
the LSM: a non-off-like target for an application group is rejected while
`PlatformFG` is not `Run`.

### D4 — Result vocabulary

`TransitionResult::ActionFailed` is appended after `Timeout`; existing values and
spellings do not move.

### D5 — No concurrency

The runtime is owned by the single arbitration thread. No atomics, locks, or
new threads are introduced.

## Risks / Trade-offs

- **LSM bypasses the generic graph** → only for explicitly bound groups;
  manifest validation ensures the table states are a subset of the group.
- **Action failure semantics** → recovery runs, the fallback state is synced,
  and the caller sees `ActionFailed`; the next request starts from the fallback.
- **EM timeout** → the LSM action fails and recovery applies, matching the
  existing SM watchdog behavior.

## Migration Plan

1. Add `function_group` and validation.
2. Add `ActionFailed` and `LsmRuntime`/sink.
3. Route TriggerIn and create the runtime in `run_sm`.
4. Add tests and docs; run the matrix.
