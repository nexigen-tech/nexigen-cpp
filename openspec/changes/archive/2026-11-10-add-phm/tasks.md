# Tasks

## 1. Vocabulary

- [x] 1.1 Create `include/nexigen/phm/health_state.hpp`: `HealthState{Ok,Warning,Error,Critical}` scoped enum with `EnumTraits`, constexpr `to_string`/`from_string`, ADL `to_json`/`from_json`, `std::formatter`. Verify: unit cases round-trip all four names and the formatter equals `to_string`.
- [x] 1.2 Create `include/nexigen/phm/supervision_status.hpp`: `SupervisionStatus{Ok,Failed,Expired,Stopped}` scoped enum with the same full conversion set. Verify: round-trip cases pass.
- [x] 1.3 Create `include/nexigen/phm/recovery_action.hpp`: `RecoveryAction{RestartProcess,SwitchFunctionGroupState,TriggerWatchdogReset}` scoped enum with the same full conversion set. Verify: round-trip cases pass.

## 2. Manifest

- [x] 2.1 Create `include/nexigen/phm/phm_manifest.hpp` (+ `.cpp`): `PhmManifest` listing `SupervisedProcess` (service id, checkpoint ids, alive cycle + margins, `CheckpointGraph` transitions, `DeadlinePair` min/max, `RecoveryRule`) with `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, member+free `to_string`, `std::formatter`. Verify: round-trip, hostile-input-unchanged, and formatter-equality cases pass.

## 3. Client library

- [x] 3.1 Create `include/nexigen/phm/health_channel.hpp`: `HealthChannel` that publishes `HealthState` transitions through `com::EventNotifier`; `to_string` + formatter. Verify: a channel transition reaches a `com::EventListener` without polling.
- [x] 3.2 Create `include/nexigen/phm/checkpoint.hpp`: `CheckpointEmitter` that emits a checkpoint id through `com::EventNotifier`; `to_string` + formatter. Verify: an emitted id reaches the listener.

## 4. Supervision engine

- [x] 4.1 Create `include/nexigen/phm/supervisor.hpp`: `Supervisor` owning per-process state machines fed by checkpoint activations; `to_string` + formatter reporting per-process status. Verify: a checkpoint for a configured process updates that process's state.
- [x] 4.2 Alive supervision: interval-tick + missed-checkpoint counting with `alive_reference_cycle` and margins. Verify: a process that stops emitting becomes `Failed` after the window.
- [x] 4.3 Logical supervision: allowed-transition graph validation. Verify: an out-of-order checkpoint reports a logical failure immediately.
- [x] 4.4 Deadline supervision: `source→target` timing with min/max. Verify: a late and an early target both report a deadline violation.
- [x] 4.5 Status aggregation: `failed_supervision_cycles_tolerance` driving `Ok → Failed → Expired`. Verify: one failure within tolerance yields `Failed`, repeated failures yield `Expired`.

## 5. Recovery

- [x] 5.1 Create `include/nexigen/phm/recovery_request.hpp`/`recovery_response.hpp`: typed `RecoveryRequest{action, pid/name/function_group}` and `RecoveryResponse` payloads (trivially copyable, `Message`-constrained) for the PHM→EM request-response channel. Verify: payloads satisfy `concepts::Message` and round-trip a `RestartProcess` action.
- [x] 5.2 Create `include/nexigen/phm/recovery.hpp`: `RecoveryClient` that sends `RestartProcess` and `SafeState` requests over `com::Client`; `to_string` + formatter reporting the last action. Verify: against a mock EM responder, an expired process produces a `RestartProcess` request carrying its identity.
- [x] 5.3 SafeState escalation: a safety-critical `Expired` status sends a `SafeState` command for the function group. Verify: the mock responder receives a `SwitchFunctionGroupState` action for the configured group.
- [x] 5.4 Fault storage: push violations onto a bounded SPSC queue drained by a dedicated thread writing fault records; the monitoring loop never blocks on I/O. Verify: a detected violation is written by the storage thread and the loop continues.

## 6. Daemon

- [x] 6.1 Create the `nexigen-phmd` tool under `tools/`: loads the JSON manifest, pins to the configured core via `core/affinity`, sets `SCHED_FIFO`, runs the `WaitSet` loop, and installs signal handling; reuses the `nexigen-hub` lifecycle pattern. Verify: the daemon starts, parses a sample manifest, and shuts down on SIGTERM.

## 7. Behavior tests

- [x] 7.1 Health-channel + checkpoint: `tests/phm/phm_test.cpp` covers state transitions and checkpoint emission. Verify: suite passes.
- [x] 7.2 Supervision: alive/logical/deadline/tolerance scenarios. Verify: suite passes.
- [x] 7.3 Recovery + fault storage against a mock EM responder. Verify: suite passes.
- [x] 7.4 Manifest round-trip + hostile input. Verify: suite passes.

## 8. Gates

- [x] 8.1 Debug build with the gate OFF stays byte-identical; gate ON builds `nexigen_phm` + `nexigen-phmd` + `nexigen_phm_tests` and all tests pass under `ctest`.
- [x] 8.2 Release (`-O3 -march=native -flto`), TSAN, and ASAN gate-ON builds pass all tests.
- [x] 8.3 `openspec validate "add-phm" --strict` passes.
