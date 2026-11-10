# Proposal

## Why

The next consumers of `nexigen::com` — the Platform Health Management daemon (`phm`) and the State Manager (`sm`) — both require a multiplexed wait over several communication sources with zero busy-polling (`REQ_PHM_012` requires `iceoryx2::waitset::WaitSet`; the SM arbitration loop blocks on it too). The `com` module does not yet expose the WaitSet, so neither consumer can be built on the protocol-independent boundary today.

## What Changes

- Add `nexigen::com::WaitSet` — a non-template wrapper over `iox2::WaitSet<Ipc>` that multiplexes an arbitrary number of attachments and blocks in the kernel until one of them fires.
- Add `WaitSetGuard` (RAII attachment handle), `WaitSetAttachmentId` (which source fired, with `has_event_from` / `has_missed_deadline`), and `CallbackProgression{Continue,Stop}` so business code never names `iox2::` in the wait callback.
- Support three attachment kinds from the start: `attach_notification` (an `EventListener`), `attach_deadline` (a notification plus a timeout — needed for PHM checkpoints and deadlines), and `attach_interval` (periodic wake-ups).
- Provide `wait_and_process`, `wait_and_process_once`, and `wait_and_process_once_with_timeout` with a `com`-typed callback.
- Map the WaitSet error enums (`WaitSetCreateError`, `WaitSetAttachmentError`, `WaitSetRunError`) into `com::Error` and extend the cold-path diagnostics (`to_string` + formatter) per the project serialization mandates.

## Capabilities

### New Capabilities

- `com/waitset`: the multiplexed kernel-level wait set over `nexigen::com` ports — attaching event listeners (with deadline/interval timers), blocking until a source fires, and dispatching the fired source to a `com`-typed callback without exposing `iox2::`.

### Modified Capabilities

None.

## Impact

- **New headers**: `include/nexigen/com/waitset.hpp`, `wait_set_guard.hpp`, `wait_set_attachment_id.hpp`, `callback_progression.hpp`.
- **Modified**: `src/com/error_mapping.cpp` (WaitSet error mappings), `include/nexigen/com/detail/error_mapping.hpp`.
- **Tests**: `tests/com/com_test.cpp` (multiplexing, deadline, interval, guard-detach, callback-progression cases) behind the existing `NEXIGEN_ENABLE_COM` gate.
- **No build changes**: the `nexigen_com` target already links `iceoryx2-cxx::static-lib-cxx` (which provides `waitset`), and the `-frtti` deviation (design D9 of `add-com-module`) is unchanged.
- **Dependencies**: none new — iceoryx2 pin is unchanged.
