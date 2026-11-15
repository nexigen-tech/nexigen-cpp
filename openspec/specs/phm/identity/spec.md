# phm/identity Specification

## Purpose
Establishes which process a checkpoint stream belongs to: a process claims its
pid over a dedicated channel, the daemon verifies the pid's owner through
`/proc`, gates checkpoints on that claim, and raises a security event when a
claim is foreign or rejected.

## Requirements

### Requirement: Checkpoint claim channel

A supervised process SHALL be able to publish a `CheckpointClaim` carrying its
name, pid, and uid on `Nexigen/PHM/Claim` and wake the paired
`Nexigen/PHM/ClaimEvent`. The daemon SHALL drain pending claims when the event
fires.

#### Scenario: A claim reaches the daemon

- **WHEN** a process publishes a claim with its name, pid, and uid
- **THEN** the daemon receives it and matches it to the manifest entry of that name

#### Scenario: An unknown name raises a security event

- **WHEN** a claim names no supervised process
- **THEN** the daemon raises a `ClaimRejected` security event and verifies nothing

#### Scenario: A lost claim is retried

- **WHEN** the daemon starts after the process, or restarts while it runs
- **THEN** the process's periodic re-claim reaches the daemon and verifies it without an explicit handshake

### Requirement: Claim verification through /proc

The daemon SHALL accept a claim only when the manifest entry declares
`expected_uid`, the claimed uid equals it, and the real uid of `/proc/<pid>` also
equals it. A pid that cannot be read SHALL be rejected.

#### Scenario: A matching UID is accepted

- **WHEN** the claim's uid and the process's real uid both equal `expected_uid`
- **THEN** the process is marked verified

#### Scenario: A foreign UID is rejected

- **WHEN** the claim or the process's real uid differs from `expected_uid`
- **THEN** the claim is rejected and a security event is raised

#### Scenario: A missing process is rejected

- **WHEN** `/proc/<pid>/status` cannot be read
- **THEN** the claim is rejected

### Requirement: Checkpoint gating

For a process whose entry declares `expected_uid`, the daemon SHALL ignore
checkpoints until that process's claim is verified. Processes without
`expected_uid` SHALL be supervised exactly as before.

#### Scenario: Unverified checkpoints are ignored

- **WHEN** a process with `expected_uid` emits checkpoints before a verified claim
- **THEN** the daemon does not feed them to the supervisor

#### Scenario: Verified checkpoints are processed

- **WHEN** a verified process emits a checkpoint
- **THEN** the supervisor processes it normally

#### Scenario: Legacy entries are unaffected

- **WHEN** a process entry has no `expected_uid`
- **THEN** its checkpoints are processed without a claim

#### Scenario: A restarted process claims again

- **WHEN** the EM reports a supervised process stopping
- **THEN** the daemon drops its verified flag, and checkpoints from the next
  incarnation are ignored until it claims again

### Requirement: Security event channel

The daemon SHALL publish a `SecurityEvent` on `Nexigen/PHM/Security` and wake
`Nexigen/PHM/SecurityEvent` for every rejected claim, carrying the kind, process
name, pid, and uid.

#### Scenario: A rejected claim is published

- **WHEN** a claim fails verification
- **THEN** a security event naming the process and the claimed pid/uid is published and its wake event fires
