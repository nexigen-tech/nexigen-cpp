# sm/ipc Specification

## Purpose

Defines the SM's external IPC surface: the `TriggerIn` request service applications use to request mode changes, the Field-based publication of the current state every process can read instantly, and the prepare-for-transition handshake.

## Requirements

### Requirement: Current state publication

The SM SHALL publish the current states of all Function Groups through the Field pattern (`history_size = 1`), so any process starting later reads the current status from shared memory immediately without sending a request.

#### Scenario: Late process reads the state

- **WHEN** a process starts after a state change
- **THEN** its first read returns the current Function Group state without any request

### Requirement: TriggerIn request service

The SM SHALL provide a `TriggerIn` service through which authorized processes request mode changes with typed fixed-size request structures (no dynamic allocations, no pointers).

#### Scenario: Application requests a mode change

- **WHEN** an authorized application sends a typed `TriggerIn` request
- **THEN** the SM receives the request payload and arbitrates it

### Requirement: Prepare-for-transition handshake

Before commanding the EM to stop processes for a transition, the SM SHALL publish a `TransitionPreparation(TargetState)` pre-notification and start a `preparation_timeout`; it SHALL send the final EM command only after the key services acknowledge or the timeout elapses.

#### Scenario: Services acknowledge before the switch

- **WHEN** a transition is prepared and the key services acknowledge
- **THEN** the SM sends the final command to the EM before the timeout

#### Scenario: Timeout forces the switch

- **WHEN** the key services do not acknowledge within `preparation_timeout`
- **THEN** the SM proceeds with the final command anyway

### Requirement: UCM channel is served by the arbitration loop

The SM SHALL open a `Nexigen/UCM/UpdateRequest` request-response server and
serve it from the arbitration loop after the TriggerIn drain. UCM handling SHALL
run on the same single writer thread; no additional thread or synchronization
SHALL be introduced.

#### Scenario: UCM request is served

- **WHEN** a UCM client sends an `UpdateRequest`
- **THEN** the arbitration loop receives it, applies the coordinator, and returns an `UpdateResponse`

#### Scenario: UCM does not block idle arbitration

- **WHEN** no UCM request is pending
- **THEN** the `serve_once` call is non-blocking and the loop remains WaitSet-driven

### Requirement: LSM-bound TriggerIn execution

The SM arbitration loop SHALL select the LSM for a bound group and run its
transition table from the same single writer thread. The selection cost SHALL be
a small in-memory lookup, with no new thread or synchronization.

#### Scenario: LSM runs in the arbitration loop

- **WHEN** a TriggerIn request arrives for a bound group
- **THEN** the LSM executes in the arbitration callback

#### Scenario: No binding keeps the existing service

- **WHEN** no LSM is bound to the requested group
- **THEN** the existing TriggerIn arbitration path runs unchanged

### Requirement: Recovery acknowledgement channel

The SM SHALL publish `RecoveryAck` samples on `Nexigen/SM/RecoveryAck` and ring `Nexigen/SM/RecoveryAckEvent` once per acknowledged recovery notification. The message SHALL be fixed-size and trivially copyable, and the publication SHALL occur on the arbitration thread with no additional thread or synchronization.

#### Scenario: Acknowledgement reaches the PHM daemon

- **WHEN** the SM acknowledges a recovery notification
- **THEN** the sample is published on the acknowledgement channel and the paired wake event is rung

#### Scenario: No PHM subscriber is harmless

- **WHEN** the SM publishes an acknowledgement and no PHM daemon is subscribed
- **THEN** the publication succeeds and arbitration continues unaffected
