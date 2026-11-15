# Proposal

## Why

`StateResult::OperationCanceled` exists but is never emitted: the state-client
transport is one-ack request-response and `StateCommand` has no request
identity. When a newer SetState supersedes an older transition, the older
requester only sees `InProgress` forever or a later unrelated result, so it
cannot tell that its request was superseded. The State Manager also retries a
pending transition without a stable identity, so the EM cannot distinguish a
retry from a new request.

## What Changes

- Add a 64-bit `request_id` to `StateCommand`; `0` means the legacy/internal
  request with no cancellation semantics.
- Track the active request id per Function Group. A newer request id cancels an
  older in-flight transition; a retry of the canceled id returns
  `OperationCanceled`. A retry of the same target with a newer id advances the
  transition.
- Make the State Manager assign one monotonically increasing id per pending
  transition, reuse it across `InProgress` retries, and drop the pending entry
  on `OperationCanceled` without forcing SafeState.
- Give the SM MachineFG lifecycle client its own request-id sequence.
- Keep the existing behavior for `request_id == 0`, all direct
  `ExecutionManager` paths, and the COM-gated control host's internal calls.

## Capabilities

### New Capabilities

- `exec/state-request-ids`: request identity, per-group ordering, cancellation,
  and stale-request rejection.

### Modified Capabilities

- `exec/state-client`: `OperationCanceled` is now emitted by the transport when
  a request is superseded; the command carries the request id.
- `sm/state-machine`: pending transitions carry a stable request id and react
  to cancellation.

## Impact

- **Performance:** one 8-byte field on the command and O(1) integer comparisons
  per request. No allocation, no new synchronization; the StateClient remains
  single-threaded.
- **Compatibility:** `request_id == 0` preserves every existing sender,
  including tests and the internal control-host path.
- **Wire size:** `StateCommand` grows from two 32-byte name buffers to two
  buffers plus `uint64_t`; all senders are updated in the same change.
