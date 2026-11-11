# Proposal

## Why

The SDV control loop is closed on the platform side (PHM daemon supervises, SM arbitrates, EM executes), but **applications have no supported way to participate**: `initial_spec/exec/adaptive_application.md` (REQ_APP_012–016) and HLR-PHM-020 require a client library — `nexigen::phm::SupervisedEntity` — through which an application reports aliveness checkpoints, logical/deadline progress, and health faults, and reacts to Function Group state changes. Today only low-level `CheckpointEmitter`/`HealthChannel` primitives exist; nothing ties them to the application lifecycle, and no application-side recovery handling exists.

## What Changes

- Add `nexigen::phm::SupervisedEntity` (HLR-PHM-020): one app-side handle that wraps the existing `CheckpointEmitter` and `HealthChannel` and provides:
  - `report_checkpoint(id)` — alive/logical/deadline progress over the iceoryx2 event pattern (REQ_APP_012/013);
  - `report_health(state)` — fault reporting into the health channel (REQ_APP_015);
  - `current_group_state(group)` — read the Function Group state the SM publishes through the Field pattern (REQ_APP_016).
- Configure it from the application identity and the PHM/SM service names; the app calls it from its `Infrastructure::run(tick)` loop (no Infrastructure policy changes).
- Applications handle a `SafeState` transition themselves: the tick reads `current_group_state()` and drives actuators to a safe mode within its limit (design decision A: Field read, no SM changes).
- Add diagnostics (`to_string` + `std::formatter`) and tests covering checkpoint/health round-trips and Function Group state reads.

## Capabilities

### New Capabilities

- `phm/supervised-entity`: the application-side PHM client — checkpoint and health reporting through the existing phm primitives, plus reading the SM-published Function Group state for recovery handling, configured from the application identity.

### Modified Capabilities

None.

## Impact

- **New header** `include/nexigen/phm/supervised_entity.hpp` (header-only, like the existing client primitives).
- **Tests** `tests/phm/` — SupervisedEntity checkpoint/health/state-read cases.
- **Dependencies**: `nexigen_phm` already links `nexigen_com`; the Function Group state read reuses the SM's published payload type (header-only reuse).
- **No changes** to `nexigen::app` (no policy change) and none to `nexigen::sm` (Field read only).
- **Deferred** (recorded in `docs/em-deferred.md`): SM event notification on state change, SRS naming rename.
