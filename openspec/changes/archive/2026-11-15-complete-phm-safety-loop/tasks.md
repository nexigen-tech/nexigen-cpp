# Tasks

## 1. Recovery vocabulary and message formats

- [x] 1.1 Add `RecoveryOutcome` (`Accepted`/`InProgress`/`Rejected`) and `RecoveryReason` enums with `to_string`/`from_string`, ADL `to_json`/`from_json`, and `std::formatter`; verify each value round-trips in the PHM enum tests.
- [x] 1.2 Add `request_id` to `RecoveryRequest` and `function_group` to the request's existing field usage; verify the request stays trivially copyable (`static_assert(com::concepts::Message)`) and round-trips through JSON.
- [x] 1.3 Replace `RecoveryResponse::accepted` with `request_id`, `outcome`, and `reason`; verify the serializer round-trips all three and that a non-object leaves the destination unchanged.
- [x] 1.4 Add the `RecoveryAck` message type and update `sm::RecoveryNotification` with `request_id`; verify both satisfy `com::concepts::Message` and keep their `to_string`/formatter output in sync.
- [x] 1.5 Rename `SupervisionStatus::Stopped` to `Exhausted` across the vocabulary table, tests, and diagnostics; verify `grep` finds no `Stopped` status name and the enum round-trip test asserts `Exhausted`.

## 2. Execution Manager: asynchronous restart

- [x] 2.1 Split the recovery server into admit and execute: validate the request, mark terminating, enqueue, and answer immediately; verify a request for a process with a long termination grace answers before the process stops.
- [x] 2.2 Emit `Rejected` with a reason for unknown process, non-operational suite, already-restarting, and unsupported action; verify each reason has a test that asserts the exact enum value.
- [x] 2.3 Make PHM-initiated restarts consume `restart_attempts`; verify the budget decreases by one per restart and that a spent budget answers `Rejected` without launching.
- [x] 2.4 Publish `Failed` once when the last permitted attempt fails; verify no further launch happens and exactly one `Failed` sample is published.
- [x] 2.5 Update the existing test that asserts PHM restarts leave the budget untouched, so it asserts the new consumption behavior; verify the suite supervision tests pass.

## 3. Execution Manager: lifecycle event content

- [x] 3.1 Add `function_group` to `ProcessLifecycleEvent` and populate it from `AppSpec` at every publish site; verify a launched application's event carries its configured group.
- [x] 3.2 Add the `Failed` state to `ProcessLifecycleState` and publish it on budget exhaustion; verify `Failed` and `Terminated` render and parse distinctly.
- [x] 3.3 Verify the event still fits the zero-copy contract and publishes without allocation; check that the steady-state test still observes no publications while every process stays in one state.

## 4. State Manager: real actuation and acknowledgement

- [x] 4.1 Make the recovery handler publish the committed group state through `StatePublication` after `force_state`; verify a Field reader observes `SafeState` after a fault event.
- [x] 4.2 Command the Emergency Manager from the recovery path and drive the pending transition in the same WaitSet callback; verify the Emergency Manager client receives a transition for the notified group.
- [x] 4.3 Send `RecoveryAck` on `Nexigen/SM/RecoveryAck` plus its wake event for every notification, with `Accepted`, `InProgress`, or `Rejected` and a reason; verify an unknown group yields `Rejected`/`UnknownGroup` and a missing Emergency Manager yields `Rejected`/`EmUnavailable`.
- [x] 4.4 Verify exactly one acknowledgement is published per drained notification and that the acknowledgement carries the notification's `request_id`.
- [x] 4.5 Update the SM recovery-handler diagnostics and its test so they assert the actuation and the acknowledgement, not only the last handled group.

## 5. Platform Health Management: routing, exhaustion, acknowledgement budget

- [x] 5.1 Carry `function_group` from `ProcessLifecycleEvent` into the supervised process state; verify an escalation names the configured group rather than the process name.
- [x] 5.2 Record an `UnknownGroup` fault and take the watchdog path when no group is known, instead of dropping the notification; verify a fault record names the missing group.
- [x] 5.3 Generate a monotonic `request_id` per recovery and put it on both the EM request and the SM notification; verify two concurrent recoveries are matched to their own acknowledgements and a stale acknowledgement is ignored.
- [x] 5.4 Add `recovery_timeout_ms` to the health manifest with a 1000 ms default, reject `0`, and include it in JSON round-trip and diagnostics; verify the manifest tests cover default, explicit value, rejection, and round-trip.
- [x] 5.5 Subscribe to the acknowledgement channel in the daemon WaitSet and apply the budget: `Accepted` keeps feeding, `Rejected` and timeout stop feeding and fire the watchdog reaction, `InProgress` does not extend the deadline; verify each of the four paths with a test.
- [x] 5.6 Map lifecycle `Failed` to `Exhausted`, suppress further restarts for that process, and escalate once; verify an `Exhausted` process that later reports `Running` returns to `Ok`.
- [x] 5.7 Stop treating an accepted restart as completed in diagnostics and fault records; verify the recovery diagnostic names the dispatched action without claiming recovery succeeded.
- [x] 5.8 Fix the fault kind recorded on expiry so a logical or deadline violation is not journaled as an alive failure; verify each violation kind produces its own record.

## 6. Integration verification

- [x] 6.1 Build all three binaries together and run the PHM, SM, and EM suites; verify every test in the three modules passes with `-j 64`.
- [x] 6.2 Run an end-to-end scenario: a supervised process stops reporting, is restarted with an `Accepted` answer, and reaches `Running`; verify the daemon resumes feeding and publishes no escalation.
- [x] 6.3 Run the failure scenario: the restart budget is exhausted, `Failed` is published, the process becomes `Exhausted`, the State Manager receives the notification, publishes `SafeState`, and acknowledges; verify the daemon keeps feeding on `Accepted`.
- [x] 6.4 Run the watchdog scenario: the State Manager does not acknowledge within `recovery_timeout_ms`; verify the daemon stops feeding and fires the watchdog reaction. Covered by `tools/nexigen-phmd/safety_loop_test.sh` modes `rejected` and `noack`.
- [x] 6.5 Run `graphify update .` after the change lands and verify the knowledge graph rebuilds without new orphan nodes.

## Workflow follow-up

- Archive the change after review.
- Verify the archived deltas landed in `openspec/specs/phm/*`, `openspec/specs/exec/*`, and `openspec/specs/sm/*`.
