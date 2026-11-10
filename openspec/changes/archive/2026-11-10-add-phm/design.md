# Design

## Context

See proposal.md - Why. The transport layer `nexigen::com` is complete (publish-subscribe, event, request-response, field, wait-set) and the Execution Manager `nexigen::exec` owns process lifecycle (start/restart/kill, `ChildProcess`, `ExecutionManager`). The PHM daemon and client library are the first infrastructure consumers of both. The original SDV contract is `initial_spec/phm/phm.md` (REQ_PHM_001–012); this change captures it as four capabilities (`phm/health-channel`, `phm/supervision`, `phm/recovery`, `phm/manifest`).

## Goals / Non-Goals

**Goals:**
- A `nexigen::phm` client library (checkpoint + health-channel emission over the com event pattern).
- A `nexigen-phmd` daemon (supervision engine + recovery + fault storage) over a single `WaitSet` loop.
- A declarative JSON manifest and a typed PHM→EM recovery channel.

**Non-Goals:**
- The EM-side `StateClient` implementation (the receiver of the `SafeState` command) — designed here as a request-response contract, implemented by the later `nexigen::sm` change.
- No YAML parser: the manifest is JSON (nlohmann, mandate 12). YAML input is deferred to a future converter; the spec's "YAML/JSON" is satisfied via JSON now.
- No integration with `sm`'s Function Groups beyond the typed `SafeState` command payload.

## Decisions

**D1 — Module + daemon split.** `nexigen::phm` (static library, `include/nexigen/phm/` + `src/phm/`) holds the client API and the supervision/recovery engine; `nexigen-phmd` (a `tools/` executable, mirroring `nexigen-hub`/`nexigen-exec`) is the daemon process that instantiates the engine and owns the `WaitSet` loop. The engine is headless/testable; the binary only wires config + affinity + signal handling.

**D2 — Transport.** Checkpoints and health-channel transitions ride the com event pattern (`EventNotifier` in the client, `EventListener` in the daemon). The daemon multiplexes every `EventListener` through `com::WaitSet` and attaches deadline/interval timers for the Alive and Deadline windows (`attach_deadline`/`attach_interval`, both already shipped). Recovery commands ride com request-response.

**D3 — Supervision engine.** One `Supervisor` owns per-process state machines fed by checkpoint activations. Alive uses interval ticks + missed-checkpoint counting; Logical walks the allowed-transition graph; Deadline starts/stops a clock per `source→target` pair. A `failed_supervision_cycles_tolerance` counter drives `Ok → Failed → Expired`. The engine exposes `SupervisionStatus` (`Ok`/`Failed`/`Expired`/`Stopped`) and a `RecoveryAction` (`RestartProcess`/`SwitchFunctionGroupState`/`TriggerWatchdogReset`) vocabulary, both with full conversion sets.

**D4 — Recovery channel + StateClient boundary.** Recovery is a request-response service (`Client<RecoveryRequest, RecoveryResponse>` in `com` terms) named by the manifest. The daemon sends `RestartProcess{pid,name}` and `SafeState{function_group,target_state}` requests. The EM-side responder is `nexigen::exec::StateClient`, designed here as the response type + request payload only; its implementation lands with `nexigen::sm`. Until then, `phm` tests exercise the channel against a mock EM responder.

**D5 — Fault storage.** Violations are pushed onto a bounded SPSC queue (reuse `core/spsc-ring-buffer`) and drained by a dedicated infrastructure thread into the fault log; the monitoring loop never blocks on I/O (REQ_PHM_008, REQ_PHM_009).

**D6 — Core isolation.** The daemon reuses `core/affinity` + `core/thread-management` to pin the monitoring thread to the configured infrastructure core and set `SCHED_FIFO` at the configured priority (REQ_PHM_009).

**D7 — Manifest.** JSON via nlohmann under `JSON_NOEXCEPTION=1`, parsed once at startup into an immutable config object (`SupervisedProcess` list, `CheckpointGraph`, `DeadlinePair`, `RecoveryRule`). The manifest is the single source of truth for the engine's parameters.

**D8 — Babbling idiot protection.** Iceoryx2's event pattern already coalesces identical triggers until the listener reads them; combined with `min_margin`/`deadline_min_duration` gating, the daemon performs `O(1)` work per cycle regardless of client flood rate (REQ_PHM_010).

## Risks / Trade-offs

- [SafeState has no live receiver until `sm`] → the recovery contract is tested against a mock EM responder; the `StateClient` implementation is explicitly tracked as the `sm` change's responsibility.
- [The daemon is a new long-running process] → it follows the existing `nexigen-hub` lifecycle (config + affinity + signal handling); a failure in it is a separate ops concern.
- [Event coalescing may hide intermediate checkpoints] → Logical/Deadline supervision consumes the latest activation per read, which matches the spec's aggregation model; ordering violations are still caught on the transition graph.
- [JSON-only manifest vs spec "YAML/JSON"] → recorded as a deliberate mandate-12 constraint; a YAML converter is a future additive change.

## Migration Plan

1. Land the `nexigen_phm` library + client API and the manifest (unit-tested) behind no new gate (depends on `NEXIGEN_ENABLE_COM` already).
2. Land the supervision engine + WaitSet loop + recovery channel with mock-EM tests.
3. Add the `nexigen-phmd` tool (config + affinity + signal handling).
4. Rollback = revert the change; `NEXIGEN_ENABLE_COM` stays OFF by default so no other module is affected.

## Open Questions

None blocking. The `StateClient` receiver is deliberately deferred to `nexigen::sm`; the request/response payloads here are the contract that change must honor.
