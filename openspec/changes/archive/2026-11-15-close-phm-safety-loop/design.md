# Design

## Context

See `proposal.md` — Why. Current state that shapes the approach:

- `phm::RecoveryClient::apply_rule` sends `RestartProcess` and
  `TriggerWatchdogReset` over one PHM→EM request-response channel; only the
  PHM side and a test double exist (`tests/phm/phm_test.cpp`).
- The suite supervisor already owns restart policy (`exec/app-suite`,
  "Restarts follow abnormal termination only") and per-attempt directories.
- PHM already has lifecycle-armed supervision: `Terminating`/`Terminated`
  suspend a process so a deliberate stop cannot look like a failure.
- The supervisor is the single writer of supervision state on the daemon
  thread, and every hot path is pre-allocated and O(1) in manifest size.

## Goals / Non-Goals

**Goals:**

- Make the existing detectors actuate: restart through the EM, reset through a
  real watchdog.
- Raise Alive and Logical supervision to the semantics of `RS_HM_09125` and
  `RS_HM_09222`.
- Keep every new hot-path cost bounded and allocation-free.
- Keep existing manifests working unchanged.

**Non-Goals:**

- Checkpoint identity (needs OS-level least privilege, `REQ_EM_009`).
- Watchdog hardware diversity beyond the standard Linux watchdog API.
- Changing the recovery wire format, the SM escalation path, or the fault log.

## Decisions

**D1 — The watchdog belongs to PHM, not the EM.** The component that decides
"the system is unhealthy" must be the component that can reset it; routing the
reset through the EM adds a hop through a process that may itself be the failing
one. `TriggerWatchdogReset` therefore stops being a request and becomes local
actuation (stop feeding). *Alternative rejected:* keep the EM as actuator — a
single point of failure on the failure path and one extra timeout.

**D2 — Feed gating is derived from supervision state, not a second heartbeat.**
The daemon feeds only when no armed process is `Expired` and the loop completed
a cycle within the feed period. `Failed` within tolerance still feeds: the
tolerance exists precisely to absorb transient failures, and resetting on the
first `Failed` would defeat it. The loop-progress condition is what protects
against a hung daemon.
*Alternative rejected:* a private heartbeat thread — it would feed a watchdog
while supervision is dead, which is worse than no watchdog.

**D3 — The device sits behind an injectable boundary.** `WatchdogDevice`
declares `open/arm/feed/disarm`; the production implementation wraps
`/dev/watchdog` (`WDIOC_SETTIMEOUT`, `write`, magic close via `write("\0")` or
`close` per the Linux API), and the test double records calls. Unit tests never
touch hardware; the real-device test is gated on the device existing and on an
explicit opt-in environment variable so a developer machine cannot reset
itself in CI.
*Alternative rejected:* direct `open()` in the daemon with `#ifdef` tests —
untestable and dangerous.

**D4 — Alive counts are additive manifest fields.** `alive_expected_indications`
plus `alive_min_margin_cycles`/`alive_max_margin_cycles`; absence keeps the
legacy rule. *Alternative rejected:* reinterpreting the existing millisecond
margins — it would silently change the meaning of deployed manifests and break
the fixed-schema contract.

**D5 — Fork/join is declared, not inferred.** An edge list cannot express "all
branches", and inference from edges is ambiguous. The manifest declares
`{fork_id, branch_entries[], join_id}`; the supervisor keeps per-process fork
state as a fixed-depth stack (max 8, rejected above) of entered-branch bitsets
(`std::uint64_t`), cleared when the join arrives. Cost per checkpoint:
`O(depth + branches/64)` compares, no allocation. Nested forks are supported by
the stack; a join with an unentered branch records a logical failure and pops the
frame.
*Alternative rejected:* a full control-flow graph engine — far more manifest
surface than the requirement needs.

**D6 — A PHM restart is a deliberate restart.** The EM force-stops the named
application (if running), relaunches it in a new attempt directory, and does not
consume `restart_attempts`; the suite reports it as a recovery restart. The
request is rejected when the suite is shutting down, the name is unknown, or the
application is already in a transition — the daemon then escalates.
*Alternative rejected:* expressing the restart as a Function Group state
transition through `StateClient` — group-grained, slower, and it would drag SM
arbitration into a per-process recovery.

**D7 — Local recovery does not retry.** The request keeps its existing timeout;
a rejected, missing, or late answer means "local recovery failed" and the daemon
applies the next rule (escalation). Retrying a wedged EM only delays the
safe state.

**D8 — No identity work in this change.** Per-service checkpoint channels
already bind a process to a service name, but any same-UID process can publish
to that name. Adding a token inside one UID would not resist a local attacker
and would create a false sense of security; the limitation is recorded in the
traceability matrix and waits for real privilege separation.

## Risks / Trade-offs

- **A watchdog can reset a developer workstation.** → Default disabled; the
  real-device test requires an explicit opt-in and always disarms on the normal
  exit path; documentation warns about `nowayout`.
- **A false `Expired` now resets hardware instead of logging.** → The tolerance
  counter and lifecycle-armed suspension already suppress deliberate stops;
  the watchdog feed gate is unit-tested against every status combination.
- **Fork/join state can leak across unrelated checkpoints.** → Frames are pushed
  only on a declared fork and popped on its declared join; an out-of-order join
  is a logical failure, so a stale frame cannot silently persist.
- **Extra count semantics can break existing manifests.** → New fields are
  optional; the legacy branch is covered by an explicit scenario and test.
- **EM restart races an operator stop.** → The control host rejects a recovery
  restart while the suite is stopping or the application is transitioning; the
  daemon escalates on rejection.

## Migration Plan

Additive. Manifests without the new sections behave exactly as today. Rolling
back means removing the server and the watchdog and ignoring the new fields; no
data or wire-format migration is involved.

## Open Questions

- Should the watchdog feed gate also require a minimum success rate over a
  window (for intermittent `Failed` cycles), rather than only "no `Expired`"?
  This can be added as a manifest policy later without changing the specs.
