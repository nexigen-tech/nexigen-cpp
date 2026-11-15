# Design

## Context

See `proposal.md` — Why. Checkpoints are event ids; the transport has no sender
identity. Least privilege now allows per-application UIDs, so the daemon can at
least check the *owner* of the claimed pid.

## Goals / Non-Goals

**Goals:**

- Move from "any process can report" to "a process with the expected UID".
- Gate checkpoints without touching the hot path for legacy entries.
- Make rejections observable through a dedicated security-event channel.

**Non-Goals:**

- Kernel-attested peer credentials (a Unix-socket `SO_PEERCRED` handshake plus
  per-process random checkpoint services). That is the only way to stop a
  same-UID impostor and is a separate change.
- Claims for processes without `expected_uid`.
- Transport ACLs or user groups.

## Decisions

**D1 — A claim sample, not a checkpoint payload change.** The checkpoint
transport stays a `u64` event id; a separate pub/sub claim carries
`{name, pid, uid}` and a paired wake event. This keeps the checkpoint hot path
and every existing manifest unaffected.
*Alternative rejected:* extending the event id with identity bits — impossible
without changing the transport.

**D2 — Verify `/proc/<pid>/status` on the daemon thread.** Verification happens
once per process incarnation, on the first claim that reaches the daemon.
Reading one small file and parsing `Uid:` is cold path; later re-claims
short-circuit on the verified flag, so no new thread or queue is needed and the
steady state costs no file reads.

**D3 — Gate only opted-in entries.** A manifest without `expected_uid` behaves
exactly as before, so existing deployments and all current tests keep working.
For an opted-in entry, an unverified checkpoint is dropped silently (a security
event on every checkpoint would flood). A claim for a legacy entry is ignored.

**D4 — One claim per incarnation plus a periodic re-claim.** The verified flag
is per process incarnation: the daemon resets it when the EM reports the
process stopping, so a restarted process must claim again. The app-side
`SupervisedEntity` re-publishes the claim once per second while it reports
checkpoints, so a daemon that starts after the process or restarts while it
runs re-learns the identity without a request/response handshake. The interval
is configurable through `ClaimConfig`.

**D5 — Accepted limitation, documented.** A same-UID process can claim another
pid and pass the check, and the checkpoint event itself still cannot be
attributed. The matrix keeps `RS_PHM_00118` at `partial`; the mechanism is a
real improvement over "any process", not a guarantee.

**D6 — Rejections are published, not just logged.** A `SecurityEvent` sample on
`Nexigen/PHM/Security` plus a wake event gives operators and future IAM code a
channel; `stderr` would be invisible in a detached daemon.

## Risks / Trade-offs

- **A claim race at startup.** → Checkpoints before the claim are dropped; the
  periodic re-claim verifies the process within one interval, and a daemon
  restart is self-healing for the same reason.
- **PID recycling.** → The claim is matched by name and the `/proc` read happens
  at claim time; a recycled pid with the right uid would still pass, which is
  the same-UID limitation.
- **The security channel has no consumer yet.** → It is an observable event
  channel; SM/logd integration is a follow-up.

## Migration Plan

Additive. To adopt: give each application its own UID, declare `expected_uid`,
and pass a `ClaimConfig` to `SupervisedEntity::create`. Removing the field
restores the legacy behavior.
