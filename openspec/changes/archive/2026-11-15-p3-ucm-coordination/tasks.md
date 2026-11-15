# Tasks

## 1. UCM vocabulary

- [x] 1.1 Add `UpdateAction`, `UpdateResult`, `UpdateRequest`, and `UpdateResponse` with the full conversion set.
- [x] 1.2 Add `TransitionResult::RejectedByUpdate` and update the SM result table/tests.

## 2. Coordinator

- [x] 2.1 Add `sm::UpdateCoordinator` with Start/Complete/Cancel, previous-state capture, and a single-update lock.
- [x] 2.2 Command the EM through `StateCommand`, retrying `InProgress` within the group timeout.
- [x] 2.3 Reject TriggerIn for locked groups with `RejectedByUpdate`; keep other groups controllable.
- [x] 2.4 Add tests for safe start, MachineFG rejection, Busy, completion, cancellation, wrong id, and EM failure.

## 3. EM Update state

- [x] 3.1 Treat `Update` as an off-like target in `FunctionGroupRegistry`.
- [x] 3.2 Add an EM test that entering `Update` stops the group and an undeclared `Update` is rejected.

## 4. Wiring and verification

- [x] 4.1 Open `Nexigen/UCM/UpdateRequest` in `nexigen-sm` and serve it from the arbitration loop.
- [x] 4.2 Add an SM integration test with a mock EM/update client.
- [x] 4.3 Update `docs/architecture.md`.
- [x] 4.4 Run Debug, Release, TSAN, ASAN and gate-OFF; run complexity/naming/result gates and `graphify update .`.
