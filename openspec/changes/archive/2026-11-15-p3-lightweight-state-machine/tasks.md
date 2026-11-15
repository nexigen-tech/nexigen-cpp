# Tasks

## 1. Vocabulary and interpreter

- [x] 1.1 Add `LsmActionType`, `LsmAction`, `LsmTransition`, `LsmErrorRecovery`, and `LightweightStateMachineConfig` with conversion sets.
- [x] 1.2 Add `LsmActionSink` and `LightweightStateMachine` with transition validation, ordered action execution, and error recovery.
- [x] 1.3 Validate action indices and state names on load.

## 2. Manifest and tests

- [x] 2.1 Add optional `state_machines` to `SmManifest` and JSON round-trip.
- [x] 2.2 Add sink-driven tests for action order, missing transition, failing action, recovery fallback, no-rule failure, and JSON round-trip.
- [x] 2.3 Update `docs/architecture.md`.

## 3. Verification

- [x] 3.1 Run Debug, Release, TSAN, ASAN and gate-OFF; run complexity/naming/result gates and `graphify update .`.
