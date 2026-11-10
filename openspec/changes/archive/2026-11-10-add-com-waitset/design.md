# Design

## Context

See proposal.md - Why. The com module wraps `iox2::*` behind `nexigen::com::*` (design D1 of `add-com-module`): every port template is non-template or payload-templated but always fixed to `iox2::ServiceType::Ipc`. The vendored iceoryx2 (pin `b0660b5b…`) already ships `iox2::WaitSet`/`WaitSetGuard`/`WaitSetAttachmentId`/`CallbackProgression` and links them through `iceoryx2-cxx::static-lib-cxx`, which the `nexigen_com` target already depends on. The com target's `-frtti` deviation (design D9 of `add-com-module`) is unaffected.

## Goals / Non-Goals

**Goals:**
- A `nexigen::com::WaitSet` whose public surface contains no `iox2::` symbol, including the wait callback signature.
- All three attachment kinds (`attach_notification`, `attach_deadline`, `attach_interval`) since PHM needs deadline timers now.
- RAII attachment lifetime and per-source identity for the dispatch callback.

**Non-Goals:**
- No general deadline/interval abstraction beyond what iceoryx2's attachments provide (a first-class `nexigen::chrono`-based scheduler is out of scope).
- No `com::WaitSetRunResult` in this change: the run-entry points return `Status` and collapse `TerminationRequest`/`Interrupt`/`StopRequest`/`AllEventsHandled` into a successful stop. Distinguishing signal-driven termination is a later concern.
- No attachment of `Subscriber`/`Client`/`Server` directly — iceoryx2's WaitSet only attaches `Listener` and file-descriptor sources; attaching other port types is not offered.

## Decisions

**D1 — Non-template `WaitSet`.** `nexigen::com::WaitSet` encapsulates `iox2::WaitSet<iox2::ServiceType::Ipc>` with no template parameter, mirroring the module's convention of fixing the transport at `Ipc` (publishers/subscribers keep only their payload parameter). Alternatives considered: exposing the `ServiceType` template parameter (rejected — leaks `iox2` and adds a dimension no com consumer needs).

**D2 — Callback adaptation without `iox2` leak.** The public entry points accept a callable `com::CallbackProgression(com::WaitSetAttachmentId)`. Internally a lambda adapts `iox2::WaitSetAttachmentId<Ipc> -> com::WaitSetAttachmentId` before invoking it and maps the returned `com::CallbackProgression` back to `iox2::CallbackProgression`. `com::WaitSetAttachmentId` owns the move-only `iox2` id and exposes `has_event_from(const WaitSetGuard&)` / `has_missed_deadline(const WaitSetGuard&)` / `hash()`. The callback is stored in `iox2::bb::StaticFunction` (copyable) exactly as iceoryx2 requires.

**D3 — Error mapping.** Add `map_error` overloads for `WaitSetCreateError`, `WaitSetAttachmentError`, and `WaitSetRunError`:
- `WaitSetCreateError::{InsufficientResources,InternalError}` -> `CreationFailed`
- `WaitSetAttachmentError::InsufficientCapacity` -> `OutOfSamples`; `AlreadyAttached` -> `InvalidArgument`; `InternalError`/`InsufficientResources` -> `CreationFailed`
- `WaitSetRunError::NoAttachments` -> `InvalidArgument`; `InsufficientPermissions` -> `CreationFailed`; `InternalError` -> `Unknown`; `TerminationRequest`/`Interrupt` -> `Ok` (a run-entry point that returns these is a normal stop, not a failure).

**D4 — File-per-entity.** `waitset.hpp` (`WaitSet`), `wait_set_guard.hpp` (`WaitSetGuard`), `wait_set_attachment_id.hpp` (`WaitSetAttachmentId`), `callback_progression.hpp` (`CallbackProgression`, a scoped enum with the full conversion set). Free `to_string` helpers and `std::formatter` specializations live beside their owners.

**D5 — Guard ownership.** `WaitSetGuard` wraps the move-only `iox2::WaitSetGuard<Ipc>`; destruction detaches (iceoryx2 behavior). `WaitSetAttachmentId` is produced from a guard via `from_guard`, matching iceoryx2's `WaitSetAttachmentId::from_guard`.

## Risks / Trade-offs

- [StaticFunction requires a copyable callback] → callables are captured by reference inside the adapter lambda (documented); callers must keep captured state alive for the duration of the wait, matching iceoryx2's own example pattern.
- [Collapsing `WaitSetRunResult` hides signal-driven termination] → acceptable for this change; `has_missed_deadline` still distinguishes the PHM deadline case, and a future change can surface the run result.
- [Callback is invoked on the calling thread with no parallelism] → this is the intended WaitSet semantics (single-waiter event loop), not a limitation.

## Migration Plan

1. Add the four headers + error mappings behind the existing `NEXIGEN_ENABLE_COM` gate.
2. Add WaitSet unit cases to `tests/com/com_test.cpp` (multiplex, deadline, interval, guard-detach, callback progression, diagnostics).
3. Rollback = revert the change; no other module is affected (the com target is gated OFF by default).

## Open Questions

None blocking.
