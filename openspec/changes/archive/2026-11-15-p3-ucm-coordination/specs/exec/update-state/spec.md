# Spec Delta

## ADDED Requirements

### Requirement: Update is an off-like EM state

The EM SHALL treat the Function Group state `Update` as off-like: entering it
SHALL stop the group's configured processes and SHALL NOT start `Update`-specific
processes in this baseline. A manifest MAY declare `Update` in a group's state
vocabulary; a group that does not declare it SHALL reject the transition with
`InvalidTransition`.

#### Scenario: Entering Update stops the group

- **WHEN** the EM receives `Update` for a group with running applications
- **THEN** those applications are stopped and the group state becomes `Update`

#### Scenario: Undeclared Update is rejected

- **WHEN** a group has no `Update` state and receives that target
- **THEN** the EM returns `InvalidTransition`

### Requirement: Daemons relaunch after Update

A daemon stopped by entering `Update` SHALL be launched again when a later
state selects it, even though the tracker records the deliberate stop as
`Exited`. Only a completed oneshot SHALL remain terminal and be skipped.

#### Scenario: Daemon returns after update

- **WHEN** a daemon is stopped by `Update` and the group later transitions to a state that configures it
- **THEN** the EM launches the daemon again

#### Scenario: Completed oneshot stays terminal

- **WHEN** a oneshot completed with exit 0 and a later state selects it
- **THEN** the EM does not launch a second instance
