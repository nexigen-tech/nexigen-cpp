# Spec Delta

## MODIFIED Requirements

### Requirement: PHM fault-event interface

The SM SHALL provide a dedicated high-priority recovery interface for the PHM daemon: a recovery-notification event channel carrying the failed Function Group and the request correlation id. On receiving a fault event the SM SHALL interrupt any ongoing non-critical transitions, commit the group state, publish the new state, enqueue the Emergency Manager transition, and initiate the failed group's transition to `SafeState` or `DegradedMode` within 1 ms of the wake.

#### Scenario: Fault event drives SafeState

- **WHEN** the PHM emits a critical fault event for a Function Group while a non-critical transition is in progress
- **THEN** the SM aborts the non-critical transition and commands the group into `SafeState` within 1 ms

#### Scenario: The new state is published

- **WHEN** the SM commits `SafeState` for a group after a fault event
- **THEN** the group's published Field carries `SafeState` before the arbitration loop waits again

#### Scenario: The Emergency Manager is commanded

- **WHEN** the SM commits `SafeState` for a group after a fault event
- **THEN** a transition request for that group reaches the Emergency Manager, not only the SM's in-memory model

#### Scenario: Unknown group is acknowledged as rejected

- **WHEN** a fault event names a Function Group the SM does not know
- **THEN** the SM publishes no state and acknowledges the notification with `Rejected` and reason `UnknownGroup`

## ADDED Requirements

### Requirement: Recovery acknowledgement event

The SM SHALL answer every recovery notification with a `RecoveryAck` event carrying the notification's request id, the outcome, and a reason, and SHALL ring its paired wake event. `Accepted` SHALL be sent once the SM has committed and published the state and enqueued the Emergency Manager transition; `InProgress` SHALL be sent when the Emergency Manager has not yet answered; `Rejected` SHALL be sent with a reason whenever the SM cannot take the group over.

#### Scenario: Accepted is sent after actuation

- **WHEN** the SM commits, publishes, and enqueues the transition for a notified group
- **THEN** it sends `RecoveryAck` with outcome `Accepted`, reason `None`, and the notification's request id

#### Scenario: Missing SafeState rejects

- **WHEN** the notified group declares neither `SafeState` nor `DegradedMode`
- **THEN** the SM sends `RecoveryAck` with outcome `Rejected` and reason `NoSafeState`

#### Scenario: No Execution Manager rejects

- **WHEN** the SM holds no Emergency Manager channel and therefore cannot command the switch
- **THEN** the SM sends `RecoveryAck` with outcome `Rejected` and reason `EmUnavailable`

#### Scenario: Every notification is answered

- **WHEN** a recovery notification is drained
- **THEN** exactly one `RecoveryAck` with its request id is published, and none is silently dropped
