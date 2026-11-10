# exec/state-client Specification

## Purpose

Defines the EM-side receiver of state commands: the `nexigen::exec::StateClient` request-response server that accepts `SetState(FunctionGroup, TargetState)` from the State Manager, maps Function Groups onto the EM's managed processes, executes start/stop, and acknowledges transition completion.

## Requirements

### Requirement: StateClient command reception

The EM SHALL expose the `StateClient` receiver as a request-response service accepting typed `SetState(FunctionGroup, TargetState)` commands from the SM; every command SHALL be acknowledged with a completion status.

#### Scenario: EM receives a SetState command

- **WHEN** the SM sends `SetState(FunctionGroup, TargetState)` over the request-response channel
- **THEN** the EM receives the typed command and acknowledges it

### Requirement: Function Group execution

On `SetState`, the EM SHALL map the Function Group onto its configured processes, start or stop them to reach the target state, and report the completed switch back to the SM; a group the EM does not manage SHALL be rejected.

#### Scenario: Group processes are switched

- **WHEN** the EM executes `SetState` for a managed group
- **THEN** the group's processes are started or stopped accordingly and the acknowledgement reports success

#### Scenario: Unknown group is rejected

- **WHEN** `SetState` names a group the EM does not manage
- **THEN** the acknowledgement reports rejection

### Requirement: StateClient diagnostics

The `StateClient` SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization naming the service and the last command processed.

#### Scenario: StateClient renders its last command

- **WHEN** `to_string()` and `std::format("{}", state_client)` are evaluated after a command
- **THEN** the text names the service and the last command, and the formatter output equals the member output
