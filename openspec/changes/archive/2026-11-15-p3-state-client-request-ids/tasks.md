# Tasks

## 1. Command identity

- [x] 1.1 Add `request_id` to `StateCommand` and its diagnostics; keep the POD contract.
- [x] 1.2 Track `request_id` and `last_completed_request_id` in the per-group transition state.
- [x] 1.3 Emit `OperationCanceled` for stale ids and superseded transitions; preserve the zero-id path.
- [x] 1.4 Add StateClient tests for same-target retries, newer/older different targets, stale post-completion retries, and zero-id compatibility.

## 2. State Manager propagation

- [x] 2.1 Allocate a per-accepted-transition id in the SM and store it in `PendingTransition`.
- [x] 2.2 Reuse the id across `InProgress` retries and drop the pending entry on `OperationCanceled` without SafeState.
- [x] 2.3 Add an independent request-id sequence to `MachineLifecycleClient`.
- [x] 2.4 Extend the SM tests to observe the id sequence and cancellation handling.

## 3. Verification and docs

- [x] 3.1 Update `docs/architecture.md` with the request-id/cancellation path.
- [x] 3.2 Run Debug, Release, TSAN, ASAN and gate-OFF; run complexity/naming/result gates and `graphify update .`.
