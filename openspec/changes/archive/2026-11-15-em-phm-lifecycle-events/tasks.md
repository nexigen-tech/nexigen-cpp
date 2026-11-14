# Tasks

## 1. Lifecycle vocabulary

- [x] 1.1 Add `include/nexigen/exec/process_lifecycle.hpp` with `ProcessLifecycleState`, the fixed-size `ProcessLifecycleEvent`, and the full conversion set.
- [x] 1.2 Add tests for enum/struct round-trip, hostile JSON, and formatter output.

## 2. EM publisher

- [x] 2.1 Add the lifecycle publisher and wake event to `ComControlHost`.
- [x] 2.2 Track the last published state per application and publish `Running` on readiness, `Terminating` on stop, and `Terminated` on reap; reset on launch.
- [x] 2.3 Add control-host tests that observe the event sequence and the no-subscriber case.

## 3. PHM lifecycle supervision

- [x] 3.1 Add per-process `EmLifecycleState` to `phm::Supervisor` with arming and suspension.
- [x] 3.2 Ignore suspended processes in Alive/Logical/Deadline handling and recovery.
- [x] 3.3 Attach the `Nexigen/PHM/ExecutionState` subscriber and wake listener to `nexigen-phmd`; drain samples in the WaitSet callback.
- [x] 3.4 Add supervisor tests for Running arming, Terminating/Terminated suspension, no false Expired, and legacy behavior without lifecycle events.

## 4. Verification and docs

- [x] 4.1 Update `docs/architecture.md` with the EM→PHM lifecycle channel.
- [x] 4.2 Run Debug, Release, TSAN, ASAN and the gate-OFF build; run complexity/naming/result gates and `graphify update .`.
