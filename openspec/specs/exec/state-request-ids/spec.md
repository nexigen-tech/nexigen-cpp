# exec/state-request-ids Specification

## Purpose
TBD - created by archiving change p3-state-client-request-ids. Update Purpose after archive.

## Requirements

### Requirement: Request identity in StateCommand

`StateCommand` SHALL carry a 64-bit `request_id`. `request_id == 0` SHALL mean
“no cancellation semantics” and keep the legacy behavior. Non-zero ids SHALL be
chosen by the requester and SHALL be stable across retries of the same pending
transition. The command SHALL remain a trivially copyable `com::concepts::Message`.

#### Scenario: Legacy zero id is accepted

- **WHEN** a command with `request_id == 0` is executed
- **THEN** the EM processes it exactly as before this change

#### Scenario: Non-zero id travels with the command

- **WHEN** a command with `request_id = 7` is sent
- **THEN** the receiver reads `7` without serialization

### Requirement: Per-group request ordering

`StateClient` SHALL track the active request id per Function Group and the last
accepted id after a completed transition. A request whose id is older than the
active id, or older than the last accepted id when no transition is active,
SHALL be rejected with `OperationCanceled` and SHALL NOT change the group state.

#### Scenario: Stale id after completion is canceled

- **WHEN** a transition with id 5 completed and a retry with id 4 arrives
- **THEN** the EM acknowledges `OperationCanceled` and the group stays in the completed state

#### Scenario: Retry with the same id advances

- **WHEN** an in-flight request with id 6 is retried with id 6 and the same target
- **THEN** the EM advances the transition and does not cancel it

### Requirement: Newer request cancels the older transition

A request with a higher id for a different target SHALL replace the in-flight
transition. A retry of the older id SHALL then return `OperationCanceled`. A
request with an id lower than or equal to the active id and a different target
SHALL be rejected as stale.

#### Scenario: Newer target wins

- **WHEN** id 10 requests `Run` and id 11 requests `Parking`
- **THEN** the EM starts the `Parking` transition and a later retry of id 10 returns `OperationCanceled`

#### Scenario: Older different target is stale

- **WHEN** id 11 is active for `Parking` and id 10 requests `Run`
- **THEN** the EM acknowledges `OperationCanceled` and keeps working toward `Parking`

### Requirement: Request-id allocation is requester-local

Request ids SHALL be monotonically increasing per requester. The EM SHALL NOT
require global uniqueness; ordering is compared only within one Function Group.
The internal RootFG lifecycle client SHALL keep its own sequence.

#### Scenario: SM and RootFG ids are independent

- **WHEN** the SM uses id 3 for an application group and the lifecycle client uses id 3 for RootFG
- **THEN** both are accepted because the groups are independent
