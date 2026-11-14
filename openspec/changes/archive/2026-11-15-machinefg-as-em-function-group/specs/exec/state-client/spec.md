# Spec Delta

## MODIFIED Requirements

### Requirement: Function Group execution

On `SetState`, the EM SHALL validate the group and target state, drive the group through the transition, and report `InProgress` while the switch is running and `Success` when the target state becomes current. A group the EM does not manage, an unknown state, or an invalid transition SHALL be rejected with the matching result; the acknowledgement SHALL never report success before the target state is reached. A `SetState` that targets `MachineFG:Off` SHALL be rejected with `InvalidTransition`: `Off` is reachable only through the EM's own shutdown path.

#### Scenario: Group processes are switched

- **WHEN** the EM executes `SetState` for a managed group and the transition completes
- **THEN** the target state becomes current and the acknowledgement reports `Success`

#### Scenario: Running transition reports InProgress

- **WHEN** the transition is still starting or stopping processes
- **THEN** the acknowledgement reports `InProgress` and the SM may retry

#### Scenario: Unknown group is rejected

- **WHEN** `SetState` names a group the EM does not manage
- **THEN** the acknowledgement reports `InvalidArgument` or `InvalidTransition`

#### Scenario: MachineFG Off is rejected

- **WHEN** `SetState` requests `MachineFG:Off`
- **THEN** the acknowledgement reports `InvalidTransition` and the group stays in its current state
