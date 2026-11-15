# Design

## Context

See `proposal.md` — Why. SM already emits a heartbeat exactly this way:
`Nexigen/SM/Health`, event id `1`, once per arbitration tick. The EM control
host (`nexigen_exec_control`) owns the supervisor's COM node and polls once per
supervision cycle.

## Goals / Non-Goals

**Goals:**

- Give the EM the same liveness signal SM has.
- Keep the dependency graph unchanged (`exec_control` must not depend on `phm`).
- Make the example deployment actually satisfy 00115/00116 on paper.

**Non-Goals:**

- Per-stage EM checkpoints (manifest load, app launch, shutdown). A single
  heartbeat is enough for liveness supervision.
- Changing PHM or the watchdog.

## Decisions

**D1 — Notify through `com::EventNotifier`, not `phm::CheckpointEmitter`.**
`CheckpointEmitter` is a three-line wrapper over the notifier; using it would
add `nexigen_exec_control → nexigen_phm`, an upward sibling dependency the
architecture avoids. The shared convention is the contract: service
`Nexigen/<cluster>/Health`, event id `1`.
*Alternative rejected:* pulling phm into exec_control for a wrapper.

**D2 — Emit once per control poll.** The poll is the supervisor's liveness
signal: if the loop stalls or the process dies, notifications stop and PHM
expires the entry. Emitting per stage would add noise without more information.

**D3 — The example manifest declares both daemons.** `sm` and `em` entries use
their health services, a 1 s alive cycle, `safety_critical: true`, and an
explicit `TriggerWatchdogReset` rule. The watchdog itself stays disabled in the
shipped example (a dev machine must not reboot); the deployment enables it.

## Risks / Trade-offs

- **A busy poll floods PHM.** → PHM's babbling-idiot margin (`alive_min_margin_ms`)
  bounds processing; the example sets it to 0 and the supervisor polls every
  `suite_poll_interval_ms`.
- **The EM heartbeat could mask a wedged app supervisor.** → It signals the
  control loop only; per-app supervision stays with the suite process table and
  PHM's own checkpoints.

## Migration Plan

Additive. Enabling supervision of the EM is a manifest change; removing the
entry restores the old behavior.
