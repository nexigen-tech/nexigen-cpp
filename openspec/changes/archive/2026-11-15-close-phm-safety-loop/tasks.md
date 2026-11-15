# Tasks

## 1. Types and manifest

- [x] 1.1 Add `WatchdogConfig` (enabled, device, timeout_ms, feed_period_ms) and the `WatchdogState` enum with `to_string`/`from_string`, ADL `to_json`/`from_json`, and `std::formatter`; verify the mandate surface with a round-trip test for every value.
- [x] 1.2 Add `alive_expected_indications`, `alive_min_margin_cycles`, and `alive_max_margin_cycles` to `SupervisedProcess` with legacy defaults; verify JSON round-trip and that an omitted field leaves zero.
- [x] 1.3 Add `LogicalFork` (`fork_id`, `branch_entries`, `join_id`) and the `logical_forks` vector to `SupervisedProcess`; verify JSON round-trip preserves order and every field.
- [x] 1.4 Validate the new manifest sections: reject negative counts, a fork entry or join absent from `checkpoints`, an empty branch list, a duplicate fork id, fork depth above 8, and `feed_period_ms` above half of `timeout_ms`; verify one test per rejection and that each names the offending process.
- [x] 1.5 Extend `PhmManifest` with the watchdog section and render it in `to_string`; verify an omitted section means disabled and the formatter output matches the member.

## 2. Supervision semantics

- [x] 2.1 Implement expected-count Alive supervision with inclusive count margins and the legacy branch when the expected count is absent or zero; verify tests for too few, too many, both inclusive bounds, and the legacy zero/one cases.
- [x] 2.2 Implement fork/join tracking as a fixed-depth (8) stack of entered-branch bitsets, cleared on the declared join; verify tests for a complete fork, a missing branch, a nested fork, an out-of-order join, and a process without forks.
- [x] 2.3 Prove the hot path stays allocation-free and bounded; verify the existing PHM allocation assertion still passes and that a checkpoint on a process with 8 forks stays within the configured cycle budget.

## 3. Watchdog

- [x] 3.1 Add the `WatchdogDevice` boundary, `PosixWatchdogDevice` (open, `WDIOC_SETTIMEOUT`, feed write, magic close), and `FakeWatchdogDevice`; verify unit tests drive open/arm/feed/disarm on the fake and the POSIX implementation compiles under the project flags.
- [x] 3.2 Implement `phm::Watchdog` with the feed policy (no `Expired`, no stalled loop, one feed per period); verify a table-driven test covers every status combination plus a stalled-loop cycle.
- [x] 3.3 Add `to_string` member plus free helper and `std::formatter` reporting device, granted timeout, feed count, and active state; verify formatter output equals the member output.
- [x] 3.4 Add an opt-in real-device test gated on the device existing and an environment variable; verify it reports skipped without the gate and disarms on every exit path.

## 4. Execution Manager recovery server

- [x] 4.1 Open `Server<RecoveryRequest, RecoveryResponse>` on `Nexigen/PHM/Recovery` in the COM control host; verify a com-level test gets an answer instead of a timeout.
- [x] 4.2 Implement `RestartProcess`: resolve the name to a suite application, force-stop it if running, relaunch it in a new attempt, answer accepted, and leave `restart_attempts` untouched; verify the attempt directory is new and the budget unchanged.
- [x] 4.3 Reject an unknown name, an application outside the suite, a non-restart action, a stopping suite, and an application in transition without changing any process; verify one test per rejection.

## 5. PHM recovery routing

- [x] 5.1 Route `TriggerWatchdogReset` to `Watchdog::stop_feeding` and stop calling the EM client for it; verify a test asserts no recovery request is sent and feeding stops.
- [x] 5.2 Treat a rejected, missing, or timed-out restart as failed local recovery and apply the next rule; verify tests for each outcome and that escalation happens exactly once.

## 6. Daemon wiring, docs, and verification

- [x] 6.1 Wire the watchdog into `nexigen-phmd` startup and shutdown (parse the section, arm after the loop starts, feed per cycle, magic-close on stop); verify a session-level test with the fake device and document the manual `softdog` run.
- [x] 6.2 Update the example PHM manifests and `docs/` with the new sections and the "disabled by default" note; verify every example manifest loads through the daemon's parser.
- [x] 6.3 Update the rows this change closes in `docs/safety/traceability.md` (`RS_HM_09125`, `RS_HM_09222`, `RS_PHM_00115`, `RS_PHM_00116`, `RS_SAF_00007`) and verify the traceability gate exits 0.
- [x] 6.4 Run `openspec validate --changes --strict` and `openspec validate --specs`; verify both exit 0.

## Workflow follow-up

- Archive the change after review.
- Verify the archived capabilities `phm/watchdog`, `phm/supervision`, `phm/manifest`, `phm/recovery`, and `exec/app-suite` reflect the new requirements.
