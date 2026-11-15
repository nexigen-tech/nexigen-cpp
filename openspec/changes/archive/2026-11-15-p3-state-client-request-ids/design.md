# Design

## Context

`StateClient` keeps one `GroupTransition` per Function Group. `begin_transition`
already replaces an older in-flight transition when a different target arrives,
but it discards the older requester's identity and returns only the new
request's result. P3 adds the identity so the older caller can be told that its
request was canceled.

## Goals / Non-Goals

**Goals:**

- Stable request identity per SetState.
- Emit `OperationCanceled` for superseded/stale requests.
- SM retries reuse the same id and drop canceled work.
- Preserve the legacy zero-id path.

**Non-Goals:**

- Asynchronous ack correlation beyond the existing request-response call.
- Multiple concurrent transitions per Function Group.
- Persisting request ids across an EM restart.
- Changing `StateAck` layout.

## Decisions

### D1 — `request_id` is a 64-bit field on `StateCommand`

`request_id == 0` means “no cancellation semantics” and is used by the
COM-gated control host's internal `request_state`, by legacy tests, and by any
caller that does not care. Non-zero ids are assigned by the requester.

### D2 — Per-group ordering and cancellation

`GroupTransition` stores `request_id` for the active transition. On
`begin_transition(index, target, request_id)`:

1. If `request_id == 0`: current behavior.
2. If a transition is active and the target is the same:
   - `request_id < active.request_id` → `OperationCanceled`;
   - `request_id >= active.request_id` → update the active id and
     `advance_transition`.
3. If a transition is active and the target differs:
   - `request_id <= active.request_id` → `OperationCanceled`;
   - otherwise cancel the old transition (log `transition_canceled`) and start
     the new one with the new id.
4. If no transition is active but `request_id < last_completed_request_id` →
   `OperationCanceled` (a stale retry after the group already moved on).
5. Otherwise process and record the id as the last accepted id.

The comparison assumes monotonically increasing ids from one requester. Ids are
not globally unique across requesters; the SM and the internal MachineFG client
each keep their own sequence, and a Function Group is owned by one requester in
practice.

### D3 — SM keeps a stable id per pending transition

`Session` owns `std::uint64_t next_request_id{1}`. When a TriggerIn request is
accepted, the SM allocates an id and stores it in `PendingTransition`. Every
`InProgress` retry sends the same id. `OperationCanceled` erases the pending
entry without forcing `SafeState` (the request was superseded, not failed).
`MachineLifecycleClient` owns a separate counter and includes it in every
command.

### D4 — No new synchronization

The StateClient remains single-threaded. `request_id` and
`last_completed_request_id` are plain `uint64_t` fields mutated only by the
control thread. No atomics, locks, shards, or memory orders are introduced.

## Risks / Trade-offs

- **Requester-local id spaces** → two requesters could reuse the same numeric id
  for different groups. The per-group comparison is still correct; a group is
  expected to have one owner.
- **Stale completion** → a canceled transition may have already started some
  processes. The new transition's `stop_unneeded`/`launch_ready_apps` brings the
  group to the newer target; this is the same behavior as the existing
  `begin_transition` replacement path.
- **Zero-id bypass** → internal callers deliberately skip cancellation. They
  are the EM itself and cannot race with an external SM in the same process.

## Migration Plan

1. Extend `StateCommand` and the transition state.
2. Emit `OperationCanceled` in `begin_transition`.
3. Add the SM request-id sequence and cancellation handling.
4. Add tests for same-target retries, superseding requests, stale ids, and the
   zero-id path.
5. Update the state-client spec and run the matrix.

## Open Questions

None blocking. A future async-ack protocol could echo `request_id` in
`StateAck`; the current synchronous call already correlates the response.
