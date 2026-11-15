# Proposal

## Why

`RS_PHM_00118` requires the daemon to accept checkpoints only from the process
they belong to, and `RS_PHM_00119` requires a security event when a foreign
process reports one. Today a checkpoint is an iceoryx2 event id with no sender
identity, so any process can notify another process's service.

Least privilege already allows per-application UIDs. This change adds the
missing bookkeeping: each supervised process claims its identity with its pid,
the daemon checks the claimed pid's owner via `/proc`, and checkpoints are
gated on a successful claim when the manifest expects a specific UID.

**Known limitation (accepted):** a malicious process running under the *same*
UID can claim another process's pid and pass the check. The mechanism raises the
bar from "any process" to "a process with the expected UID"; full protection
needs kernel-attested peer credentials (a Unix-socket handshake), which is
deferred. `RS_PHM_00118` therefore stays `partial`; `RS_PHM_00119` closes.

## What Changes

- A supervised process entry gains `expected_uid` (optional).
- New `phm::CheckpointClaim` message and `phm::ClaimPublisher`: a process
  publishes `{name, pid, uid}` on `Nexigen/PHM/Claim` and wakes
  `Nexigen/PHM/ClaimEvent`.
- New `phm::checkpoint_claim_verdict`: reads `/proc/<pid>/status`, and accepts
  the claim only when the claimed UID and the process's real UID both equal
  `expected_uid`.
- New `phm::ClaimRegistry`: transport-free gate that applies claims and answers
  whether a process's checkpoints may reach the supervisor.
- `phm::SupervisedEntity` optionally owns a claim channel (a `ClaimConfig`) and
  re-publishes the claim once per second, so a late or restarted daemon
  re-learns the identity; the daemon drops the verification when the EM reports
  the process stopping.
- The daemon subscribes to claims, marks a process verified, and raises a
  `phm::SecurityEvent` on `Nexigen/PHM/Security` (plus a wake event) for an
  unknown name or a rejected claim.
- For a process with `expected_uid` set, checkpoints are ignored until its claim
  is verified.
- `docs/architecture.md` documents the limitation.

## Capabilities

### New Capabilities

- `phm/identity`: claim channel, UID verification, checkpoint gating, and the
  security-event channel.

### Modified Capabilities

- `phm/manifest`: the supervised process gains `expected_uid`.
- `phm/supervised-entity`: the client handle gains an optional identity-claim
  configuration and a periodic re-claim.

## Impact

- **Code**: new `phm` headers and `src/phm/checkpoint_identity.cpp`, the PHM
  daemon session/loop, and the manifest parser.
- **Performance**: claim verification happens once per process at startup and
  reads one small `/proc` file; the checkpoint path only gains one boolean check
  for processes that opt in. No allocation on the checkpoint path.
- **Memory**: one verified flag per supervised process.
- **Compatibility**: additive. A manifest without `expected_uid` accepts
  checkpoints exactly as before.
