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
