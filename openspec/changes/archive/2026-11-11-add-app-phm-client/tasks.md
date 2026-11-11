# Tasks

## 1. SupervisedEntity

- [x] 1.1 Create `include/nexigen/phm/supervised_entity.hpp`: `SupervisedEntity` composing `CheckpointEmitter` + `HealthChannel`; constructed on a `com::Node` from the app identity and the checkpoint/health service names; `to_string` + `std::formatter`. Verify: creation opens both channels; `to_string` equals the formatter output.
- [x] 1.2 `report_checkpoint(checkpoint_id)`: publish the id through the checkpoint channel; track the last id for diagnostics. Verify: a reported id reaches a `com::EventListener`.
- [x] 1.3 `report_health(state)`: publish a `HealthState` through the health channel; track the last state. Verify: a reported `Error` reaches the daemon-side listener.

## 2. Function Group state

- [x] 2.1 `current_group_state(group)`: non-blocking read of the SM-published `Field` (`Nexigen/SM/State/<group>`); reports no state when the group is unpublished. Verify: a published `SafeState` is read back; an unknown group yields no state without blocking.

## 3. Tests

- [x] 3.1 `tests/phm/` (gated ON): checkpoint round-trip, health round-trip, Function Group state read (published and unknown), diagnostics. Verify: suite passes.
- [x] 3.2 Example: wire a heartbeat `report_checkpoint` + `current_group_state` SafeState check into the application example's run loop. Verify: the example builds and runs its cycle.

## 4. Gates

- [x] 4.1 Debug build with the gate OFF stays byte-identical; gate ON builds and all tests pass under `ctest`.
- [x] 4.2 Release (`-O3 -march=native -flto`), TSAN, and ASAN gate-ON builds pass all tests.
- [x] 4.3 `openspec validate "add-app-phm-client" --strict` passes.
