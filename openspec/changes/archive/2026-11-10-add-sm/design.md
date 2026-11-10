# Design

## Context

See proposal.md - Why. The transport (`nexigen::com`: event, wait-set, request-response, field), the health layer (`nexigen::phm`), and the Execution Manager (`nexigen::exec`: process start/stop/restart, `ExecutionManager`, `ChildProcess`) are complete. The State Manager is the last infrastructure module: the central arbiter that closes the PHM→SM→EM loop. The SDV contract is `initial_spec/sm/HLR_02_State_Management_Architecture.md` + `initial_spec/sm/state_manager_srs.md` (REQ_SM_001–019).

## Goals / Non-Goals

**Goals:**
- The `nexigen::sm` arbitration engine + the `nexigen-sm` foreground process under EM control.
- The full PHM→SM→EM recovery loop: PHM emits a fault event to SM, SM arbitrates, SM commands EM via `nexigen::exec::StateClient`, EM executes start/stop and acknowledges.
- State publication through the Field pattern, TriggerIn request service, prepare-for-transition handshake, transition watchdog, audit ring buffer, and metrics.

**Non-Goals:**
- `REQ_SM_008` (OTA/UCM coordination) — deferred until the update manager exists (recorded here).
- EM-side `cgroups` placement (REQ_SM_012 item 1) is the EM's configuration concern, not SM code; `mlockall` and allocation-free arbitration ARE implemented in SM.

## Decisions

**D1 — Process model.** `nexigen::sm` is a static library with a headless `StateManager` engine; `nexigen-sm` (a `tools/` executable, like `nexigen-phmd`) is the foreground process the EM launches with CLI/env config. It never daemonizes (REQ_SM_001); readiness reporting reuses the exec app-lifecycle reporting path.

**D2 — Transport.** `TriggerIn` is a request-response service with the SM as server (typed, fixed-size `TriggerRequest`/`TriggerResponse` payloads). Current states are published through `com::Field` per Function Group (`history_size=1`, REQ_SM_009). The PHM fault channel is a `com::EventListener` in SM (REQ_SM_007). `StateClient` is a request-response service with the EM as server (`SetStateCommand`/`SetStateAck` payloads). All four ride the existing `com` patterns; the SM's arbitration thread multiplexes everything through one `com::WaitSet`.

**D3 — Arbitration engine.** The manifest declares Function Groups with string states and `from -> to` transition tables; the engine validates requests against the graph (`InvalidTransition`), resolves conflicts by configured `requester_priorities` (PHM highest), and drives `MachineState` sequencing (`Startup -> Verify -> NormalRunning`, and the shutdown chain). A `TransitionResult` enum (`Success`/`RejectedByPriority`/`InvalidGraph`/`Timeout`) with a full conversion set is the arbitration outcome vocabulary.

**D4 — StateClient receiver in exec.** `nexigen::exec::StateClient` wraps a `com::Server<SetStateCommand, SetStateAck>`. A command maps `FunctionGroup -> configured processes` (an EM-side manifest section) and invokes the existing `ExecutionManager` start/stop machinery; the ack carries completion or rejection (unknown group). The receiver runs on the EM's control thread and integrates with the existing signal/lifecycle handling.

**D5 — PHM SafeState re-route.** `phm`'s recovery changes per the user decision: `RecoveryClient::switch_function_group`/`trigger_watchdog_reset` semantics move to a recovery-notification EVENT to the SM channel (`Nexigen/SM/Recovery`); `restart_process` stays a direct PHM→EM request (REQ_PHM_006 is an EM concern). The `phm/recovery` main spec is updated via the MODIFIED delta; the `RecoveryRequest` payload keeps `RestartProcess` and adds the fault-event payload for SM.

**D6 — Lifecycle and resources.** The arbitration thread pins to `EASY_SM_RT_CPU_MASK` under `SCHED_FIFO` (priority below PHM, above RT apps) via `core/thread_management`; flusher threads pin to `EASY_BG_CPU_MASK` under `SCHED_OTHER`. `mlockall(MCL_CURRENT | MCL_FUTURE)` at init; no allocation in the arbitration loop (all buffers pre-allocated). Transition watchdog: a `WaitSet` deadline attachment per active transition (`transition_max_time`), forcing `SafeState`/`Off` on timeout. Self-health: a checkpoint emission to PHM each cycle (REQ_SM_002).

**D7 — Telemetry.** Audit records go into a pre-allocated lock-free SPSC ring (reuse `core::SpscRingBuffer`) in `O(1)`; a background flusher thread formats them into the fault/audit log. Metrics are `alignas(64)` relaxed atomics (`last_transition_duration_us`, `arbitration_latency_us`, `rejected_requests_total`, `emergency_transitions_total`, `transition_timeouts_total`) flushed periodically by the background thread.

**D8 — Manifest.** JSON via nlohmann (`JSON_NOEXCEPTION=1`): `function_groups` with `states`, `allowed_transitions`, `transition_max_time_ms`, `preparation_timeout_ms`, `requester_priorities`, `metrics_flush_interval_ms`. Parsed once at startup into immutable structures; YAML input deferred (mandate 12), as in `phm`.

## Risks / Trade-offs

- [FunctionGroup→process mapping is the EM's new responsibility] → the mapping lives in the EM's own manifest section; an unmapped group yields a rejection ack, keeping SM and EM decoupled.
- [1 ms SafeState reaction depends on scheduler behavior] → the arbitration thread is SCHED_FIFO at the configured priority; the 1 ms bound is validated by the latency tests under the existing `NEXIGEN_STRICT_LATENCY_TESTS` gates.
- [PHM SafeState re-route changes shipped behavior] → the `phm/recovery` MODIFIED delta updates the main spec; `RestartProcess` behavior is unchanged.
- [SM is a critical single point] → PHM supervises SM itself (REQ_SM_002); an SM hang triggers the PHM→EM emergency restart path.

## Migration Plan

1. Vocabulary + manifest (unit-tested) → arbitration engine + state machines → IPC surface (TriggerIn, Field publication, prepare handshake).
2. `StateClient` receiver in `exec` + the FunctionGroup mapping, then the PHM SafeState re-route with updated phm tests.
3. The `nexigen-sm` process (config + affinity + WaitSet loop + watchdog + telemetry), then full-gate validation.
4. Rollback = revert the change; `NEXIGEN_ENABLE_COM` stays OFF by default.

## Open Questions

None blocking. `REQ_SM_008` (OTA/UCM) is deliberately deferred until the update manager module exists.
