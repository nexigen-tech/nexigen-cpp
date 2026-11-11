# phm/supervised-entity Specification

## Purpose

Defines the application-side PHM client: `nexigen::phm::SupervisedEntity` through which an application reports aliveness/logical/deadline checkpoints and health faults over iceoryx2, and reads the Function Group state published by the State Manager so it can react to a `SafeState` transition.

## Requirements

### Requirement: Supervised entity configuration

`nexigen::phm::SupervisedEntity` SHALL be created on the application's `com::Node` from the application identity and the PHM checkpoint/health service names; creation SHALL succeed only when the underlying checkpoint and health channels open.

#### Scenario: Entity opens its channels

- **WHEN** an application creates a `SupervisedEntity` with valid service names
- **THEN** the checkpoint and health channels are open and the entity is ready to report

### Requirement: Checkpoint reporting

`SupervisedEntity::report_checkpoint(checkpoint_id)` SHALL publish the checkpoint id to the PHM daemon over the iceoryx2 event pattern; an application calls it from each critical cyclic thread, and the sequence START → intermediate → END SHALL carry the logical and deadline progress the daemon supervises.

#### Scenario: Checkpoint reaches the daemon

- **WHEN** an application reports checkpoint id `7` from its run loop
- **THEN** the daemon's listener receives the activation identifying checkpoint `7`

#### Scenario: Deadline sequence is reported in order

- **WHEN** an application reports the START checkpoint, intermediate checkpoints, and the END checkpoint of one algorithm
- **THEN** the daemon observes them in that order and can measure the START→END duration

### Requirement: Health status reporting

`SupervisedEntity::report_health(state)` SHALL publish a `HealthState` transition to the PHM daemon through the health channel, so an application that cannot recover a fault reports it immediately.

#### Scenario: Fault is reported

- **WHEN** an application detects an unrecoverable internal fault and reports `Error`
- **THEN** the daemon's health listener observes the `Error` state for that application

### Requirement: Function Group state read

`SupervisedEntity::current_group_state(group)` SHALL read the current Function Group state published by the State Manager through the Field pattern, without blocking and without polling the state machine; an application reads it in its run tick and drives its actuators to the safe mode when the state becomes `SafeState`.

#### Scenario: Application observes SafeState

- **WHEN** the State Manager publishes `SafeState` for the application's Function Group and the application calls `current_group_state(group)` in its tick
- **THEN** the call returns `SafeState` and the application can begin its safe-mode sequence

#### Scenario: Unpublished group reads empty

- **WHEN** the requested Function Group has no published state
- **THEN** the call reports no state instead of blocking

### Requirement: Supervised entity diagnostics

`SupervisedEntity` SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization naming the application and the last reported checkpoint/health state.

#### Scenario: Entity renders its state

- **WHEN** `to_string()` and `std::format("{}", entity)` are evaluated after a checkpoint report
- **THEN** the text names the application and the last checkpoint/state, and the formatter output equals the member output
