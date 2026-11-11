# Design

## Context

See proposal.md - Why. The phm client primitives already exist (`CheckpointEmitter`, `HealthChannel` over `com::EventNotifier`), the SM publishes Function Group states as `com::Field<GroupStatePayload>` on `Nexigen/SM/State/<group>` (`history_size = 1`), and the application lifecycle is `Infrastructure::initialize()` + `Infrastructure::run(tick)` — a `while (!stop_requested())` loop calling the tick. This change adds the app-facing handle that ties those together.

## Goals / Non-Goals

**Goals:**
- `nexigen::phm::SupervisedEntity`: checkpoint reporting, health reporting, Function Group state read, diagnostics.
- Zero changes to `nexigen::app` (no policy change) and `nexigen::sm` (Field read only).

**Non-Goals:**
- SM event notification on state change (recorded in `docs/em-deferred.md`; today the app reads the Field in its tick).
- Automatic heartbeat thread inside `Infrastructure` (the application calls `report_checkpoint` from its own critical threads, per REQ_APP_012).
- SRS naming rename (`nexigen::log`/`nexigen::trace`) — separate breaking change.
- REQ_APP_004 sandbox — production hardening.

## Decisions

**D1 — Separate entity, no Infrastructure policy.** Unlike logging/metrics/tracing, PHM participation is per-critical-thread and conditional; forcing it into the compile-time `InfrastructurePolicy` would burden every application. `SupervisedEntity` is an ordinary value created in `main` (or the app's setup) and called from the run loop.

**D2 — Reuse the existing primitives.** `SupervisedEntity` composes a `CheckpointEmitter` (event) and a `HealthChannel` (event); checkpoints are just ids, so alive/logical/deadline supervision needs no new transport (the PHM daemon already interprets the graph/deadlines).

**D3 — Function Group state read via the SM Field.** `current_group_state(group)` opens the `com::FieldListener<GroupStatePayload>` for `Nexigen/SM/State/<group>` (the payload type is the SM's published type, reused header-only; `nexigen_phm` already links `com`). The call is non-blocking; an absent/unpublished group returns no state. This is decision A from the exploration: the application reacts in its own tick, no SM change.

**D4 — Diagnostics.** `to_string()` names the application plus the last checkpoint id and last `HealthState`; a `std::formatter` delegates to it (mandates 9/11).

## Risks / Trade-offs

- [Header-only reuse of an SM payload type creates a phm→sm header dependency] → only the trivially-copyable payload type is reused (no link dependency); documented and small.
- [The app must call `report_checkpoint` from its critical thread] → that is exactly what REQ_APP_012 prescribes; a convenience wrapper stays out of scope.
- [Field read returns the last published value, not an event] → acceptable per decision A; the future SM event notification is tracked in `docs/em-deferred.md`.

## Migration Plan

1. Add `supervised_entity.hpp` (header-only) + tests (checkpoint/health round-trip, group-state read).
2. Wire a heartbeat into the example application to demonstrate the run-loop integration.
3. Rollback = revert the change; no other module is affected.

## Open Questions

None blocking.
