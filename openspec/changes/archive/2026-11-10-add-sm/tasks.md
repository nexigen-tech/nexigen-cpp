# Tasks

## 1. Vocabulary

- [x] 1.1 Create `include/nexigen/sm/transition_result.hpp`: `TransitionResult{Success,RejectedByPriority,InvalidGraph,Timeout}` scoped enum with `EnumTraits`, constexpr `to_string`/`from_string`, ADL `to_json`/`from_json`, `std::formatter`. Verify: round-trip cases pass.

## 2. Manifest

- [x] 2.1 Create `include/nexigen/sm/sm_manifest.hpp` (+ `.cpp`): `SmManifest` with `FunctionGroup` (`states`, `allowed_transitions` from/to, `transition_max_time_ms`, `preparation_timeout_ms`), `requester_priorities`, `metrics_flush_interval_ms`; JSON round-trip under `JSON_NOEXCEPTION=1`, member+free `to_string`, `std::formatter`. Verify: round-trip, hostile-input-unchanged, formatter-equality cases pass.

## 3. Arbitration engine

- [x] 3.1 Create `include/nexigen/sm/state_manager.hpp` (+ `.cpp`): headless `StateManager` owning the `MachineState` machine and per-group FunctionGroup machines; `request_transition(group, target, requester_id, now)` validates the graph and returns `TransitionResult`; `to_string` + formatter. Verify: an invalid transition returns `InvalidGraph` and the state stays.
- [x] 3.2 Request arbitration: conflicting requests resolved by `requester_priorities` (PHM highest). Verify: a higher-priority requester wins over a lower one.
- [x] 3.3 Startup/shutdown sequencing: `Startup -> Verify -> NormalRunning` advancement and the shutdown chain (`Off` for every application group, then `MachineState::Shutdown`). Verify: sequencing scenarios pass.

## 4. IPC surface

- [x] 4.1 Create `include/nexigen/sm/trigger_request.hpp`/`trigger_response.hpp`: typed fixed-size `TriggerIn` payloads (trivially copyable, `Message`-constrained). Verify: payloads satisfy `concepts::Message`.
- [x] 4.2 Current-state publication: publish every Function Group state through `com::Field` (`history_size=1`). Verify: a late reader reads the current state immediately.
- [x] 4.3 Prepare-for-transition: publish `TransitionPreparation(TargetState)` and start `preparation_timeout` before the final EM command; proceed on acknowledgements or timeout. Verify: both paths (ack and timeout) pass.

## 5. Recovery handler

- [x] 5.1 Create `include/nexigen/sm/recovery_handler.hpp`: `RecoveryHandler` listening for PHM fault events (`com::EventListener` on the recovery channel); on a fault event it interrupts non-critical transitions and initiates the failed group's `SafeState`/`DegradedMode`. Verify: a fault event drives the group to `SafeState` within the arbitration cycle.

## 6. StateClient receiver in exec

- [x] 6.1 Create `include/nexigen/exec/state_command.hpp`/`state_ack.hpp`: typed `SetStateCommand{function_group, target_state}` and `SetStateAck{result}` payloads (trivially copyable). Verify: `Message` static asserts hold.
- [x] 6.2 Create `include/nexigen/exec/state_client.hpp` (+ `.cpp`): `StateClient` wrapping a `com::Server<SetStateCommand, SetStateAck>`; maps FunctionGroup -> configured processes (EM manifest section) and invokes the existing `ExecutionManager` start/stop machinery; acks completion or rejection (unknown group); `to_string` + formatter. Verify: a mock SM command for a mapped group acks success, an unmapped group acks rejection.

## 7. PHM SafeState re-route

- [x] 7.1 Change `phm` recovery: `RecoveryClient` SafeState escalation emits a recovery-notification EVENT to the SM channel (`Nexigen/SM/Recovery`) instead of a direct EM request; `RestartProcess` stays a direct PHM→EM request. Verify: the updated phm recovery tests observe the fault event on the SM channel and the unchanged RestartProcess behavior.

## 8. Lifecycle and resources

- [x] 8.1 Managed startup + readiness report: `nexigen-sm` parses CLI/env config, opens channels, pins threads, reports `kRunning` within `startup_timeout`. Verify: the tool starts and reports readiness.
- [x] 8.2 Self-health: the arbitration cycle emits a checkpoint to PHM each cycle. Verify: the PHM listener receives checkpoints.
- [x] 8.3 WaitSet loop + transition watchdog: one `WaitSet` multiplexes PHM/EM/TriggerIn channels; each active transition is bounded by a `transition_max_time` deadline attachment forcing `SafeState`/`Off` on timeout. Verify: idle CPU is 0 and a stalled transition times out.
- [x] 8.4 Resources: `mlockall(MCL_CURRENT | MCL_FUTURE)`, RT-core pin + `SCHED_FIFO` for the arbitration thread, background-core pin + `SCHED_OTHER` for flushers, no allocation in the arbitration loop. Verify: affinity/scheduler tests pass.

## 9. Telemetry

- [x] 9.1 Audit ring buffer: transition records (timestamp, group, old/new state, requester id, `TransitionResult`) pushed to a pre-allocated lock-free SPSC ring in O(1); a background flusher thread writes them. Verify: records are journaled without blocking the arbitration thread.
- [x] 9.2 Metrics: `alignas(64)` relaxed atomics (`last_transition_duration_us`, `arbitration_latency_us`, `rejected_requests_total`, `emergency_transitions_total`, `transition_timeouts_total`) flushed periodically. Verify: counters reflect events and `to_string`/formatter report them.

## 10. Behavior tests

- [x] 10.1 `tests/sm/sm_test.cpp`: state machine, arbitration, sequencing, TriggerIn, Field publication, prepare handshake, recovery handler, watchdog, telemetry, manifest. Verify: suite passes.
- [x] 10.2 `tests/exec` StateClient cases against a mock SM client. Verify: suite passes.
- [x] 10.3 Updated `tests/phm` recovery cases (SafeState re-route + RestartProcess unchanged). Verify: suite passes.

## 11. Gates

- [x] 11.1 Debug build with the gate OFF stays byte-identical; gate ON builds `nexigen_sm`, `nexigen-sm`, `nexigen_sm_tests` and all tests pass under `ctest`.
- [x] 11.2 Release (`-O3 -march=native -flto`), TSAN, and ASAN gate-ON builds pass all tests.
- [x] 11.3 `openspec validate "add-sm" --strict` passes.
