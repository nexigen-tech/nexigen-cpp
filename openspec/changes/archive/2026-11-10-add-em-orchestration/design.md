# Design

## Context

See proposal.md - Why. The EM (`nexigen::exec`) is a lifecycle supervisor with start/stop/restart, per-application CPU affinity, suite supervision, and (from add-sm) a `StateClient` request-response receiver. The audit of `initial_spec/exec/execution_manager.md` (2026-11-10) left orchestration gaps: no dependency tree, no Function Group configuration, file-based readiness instead of the iceoryx2 event channel, and FIFO command processing in `StateClient`. The transport (`nexigen::com` event/wait-set/request-response) and the PHM/SM layers are complete, so all five gaps are closable on top of them.

## Goals / Non-Goals

**Goals:**
- Dependency-ordered startup gated on the `Running` readiness event.
- EM-side Function Groups with group-bound process start/stop.
- Highest-priority SafeState handling in `StateClient`.
- Startup-timeout recovery policy and crash cleanup before restart.

**Non-Goals:**
- Security hardening (REQ_EM_009/010) — deferred, `docs/em-deferred.md`.
- OTA/UCM (REQ_SM_008) — deferred.
- YAML manifests — deferred (JSON-only, mandate 12).
- Full `cgroups` quotas and per-application scheduler policies (REQ_EM_008) — deferred with the resource wave.

## Decisions

**D1 — Readiness channel.** `ReportApplicationState(Running)` rides the com event pattern: each application emits the event on a per-application channel derived from its name (`Nexigen/EM/State/<app>`), and the EM attaches the corresponding `EventListener` through its supervision WaitSet (REQ_EM_011). The existing file-based `resource.json` status stays as the persistent record; the event is the real-time gate signal.

**D2 — Dependency gating.** `AppSpec` gains `dependencies` (a list of application names). The suite supervisor starts only applications whose dependencies have reported `Running`; the readiness signal is the D1 event (with the file state as the fallback for applications that only write `resource.json`). A dependency that fails or times out fails the dependents' startup visibly (REQ_EM_001/002).

**D3 — Function Groups.** The suite manifest gains `function_groups` with `states` per group, and `AppSpec` gains the list of groups that keep the application active. `StateClient::execute` consults the EM's group registry (group -> bound processes) instead of its own `GroupMapping` vector; a group state switch stops (Off-like targets) or starts (active targets) exactly the bound processes, using the graceful shutdown sequence (REQ_EM_005/006).

**D4 — SafeState priority.** `StateClient` listens on two request-response channels: the ordinary `SetState` channel and an emergency channel (`<channel>Emergency`) reserved for PHM `SafeState` commands. `serve_once` drains the emergency channel first, so a SafeState command is executed and acknowledged before any queued ordinary transition; ordinary commands keep FIFO among themselves (REQ_EM_007). The SM routes SafeState-target commands to the emergency channel; the priority is therefore a transport-level guarantee, not a best-effort reorder.

**D5 — Recovery policy + crash cleanup.** The startup-timeout path (SIGTERM → grace → SIGKILL on `startup_timeout`) logs the failure through `nexigen::log` and feeds the existing `restart_attempts` machinery instead of stopping silently (REQ_EM_002). Before a restart, the EM runs the iceoryx2 cleanup of stale node markers for that application's identity (REQ_EM_014 item 3); the transport's automatic cleanup covers the loaned-sample reclamation (already verified by the com two-process tests).

## Risks / Trade-offs

- [Readiness event and file state can disagree] → the event is the gate signal, the file is the persistent record; the supervisor prefers the event and reconciles on startup (documented per-application state machine).
- [Function Groups duplicate some SM state] → the EM's group registry is the *execution* mapping (groups -> processes), while the SM owns the *arbitration*; both derive from their own manifests, as the SDV layering prescribes.
- [Priority dispatch reorders the command stream] → only SafeState overtakes; ordinary commands keep FIFO among themselves (spec scenario).
- [Crash cleanup may race a live relaunch] → cleanup runs in the restart path only, bounded by the identity check already in place (`proc_start_ticks`).

## Migration Plan

1. Readiness event (D1) + `AppSpec.dependencies` gating (D2), unit-tested behind the existing `NEXIGEN_ENABLE_COM` gate.
2. Function Groups registry (D3) wired into `StateClient`, SafeState priority (D4).
3. Startup-timeout recovery policy + crash cleanup (D5), then full-gate validation.
4. Rollback = revert the change; `NEXIGEN_ENABLE_COM` stays OFF by default.

## Open Questions

None blocking.
