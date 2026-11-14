# exec/state-client Specification

## Purpose

Defines the EM-side receiver of state commands: the `nexigen::exec::StateClient` request-response server that accepts `SetState(FunctionGroup, TargetState)` from the State Manager, maps Function Groups onto the EM's managed processes, executes start/stop, and acknowledges transition completion.

## Requirements

### Requirement: StateClient command reception

The EM SHALL expose the `StateClient` receiver as a request-response service accepting typed `SetState(FunctionGroup, TargetState)` commands from the SM. Every command SHALL be acknowledged with an acceptance flag and a result value from `Success`, `InProgress`, `OperationFailed`, `OperationCanceled`, `UnexpectedTermination`, `InvalidTransition`, or `InvalidArgument`; the result vocabulary SHALL round-trip through text, formatting, and JSON. `OperationCanceled` is reserved for a future request-id/async-ack protocol and is not emitted by the current one-ack transport.

#### Scenario: EM receives a SetState command

- **WHEN** the SM sends `SetState(FunctionGroup, TargetState)` over the request-response channel
- **THEN** the EM receives the typed command and acknowledges it with an acceptance flag and result

#### Scenario: Result vocabulary round-trips

- **WHEN** each state-result value passes through `to_string`, `from_string`, `to_json`, `from_json`, and `std::format`
- **THEN** every canonical name round-trips and the formatter renders the name

### Requirement: Function Group execution

On `SetState`, the EM SHALL validate the group and target state, drive the group through the transition, and report `InProgress` while the switch is running and `Success` when the target state becomes current. A group the EM does not manage, an unknown state, or an invalid transition SHALL be rejected with the matching result; the acknowledgement SHALL never report success before the target state is reached.

#### Scenario: Group processes are switched

- **WHEN** the EM executes `SetState` for a managed group and the transition completes
- **THEN** the target state becomes current and the acknowledgement reports `Success`

#### Scenario: Running transition reports InProgress

- **WHEN** the transition is still starting or stopping processes
- **THEN** the acknowledgement reports `InProgress` and the SM may retry

#### Scenario: Unknown group is rejected

- **WHEN** `SetState` names a group the EM does not manage
- **THEN** the acknowledgement reports `InvalidArgument` or `InvalidTransition`

### Requirement: StateClient diagnostics

The `StateClient` SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization naming the service and the last command processed.

#### Scenario: StateClient renders its last command

- **WHEN** `to_string()` and `std::format("{}", state_client)` are evaluated after a command
- **THEN** the text names the service and the last command, and the formatter output equals the member output

### Requirement: SafeState priority

When the `StateClient` receives a `SafeState` command from the PHM daemon, the EM SHALL process it with the highest priority: any ordinary queued transition SHALL wait, and the SafeState switch SHALL be executed and acknowledged first.

#### Scenario: SafeState overtakes queued transitions

- **WHEN** ordinary transitions are queued and a SafeState command arrives
- **THEN** the EM executes and acknowledges the SafeState switch before the queued ordinary transitions

#### Scenario: Ordinary transitions proceed afterwards

- **WHEN** the SafeState switch has completed
- **THEN** the queued ordinary transitions resume processing in their original order

### Requirement: In-flight transition cancellation

A Function Group SHALL have at most one transition in flight. A new validated request for a different state SHALL cancel the in-flight transition and start the new transition; the old request’s `InProgress` is superseded, so the new acknowledgement is the observable result. A request for the state already current or already requested SHALL be ignored and reported as `Success`.

#### Scenario: New request replaces the in-flight transition

- **WHEN** a transition is in progress and a validated request for a different state arrives
- **THEN** the old transition is cancelled and the new transition starts

#### Scenario: Same-state request succeeds

- **WHEN** the group is already in the requested state
- **THEN** no transition starts and the acknowledgement reports `Success`

### Requirement: Transition failure result

When a transition stops because the requested state cannot be reached, the EM SHALL set the group to `Undefined` and return `OperationFailed` or `UnexpectedTermination` as appropriate. A termination timeout that still reaches the requested state SHALL return `Success`.

#### Scenario: Startup exhaustion reports failure

- **WHEN** a requested-state process exhausts its startup attempts
- **THEN** the acknowledgement reports `OperationFailed` and the group state is `Undefined`

#### Scenario: Unexpected termination reports failure

- **WHEN** a requested-state process terminates unexpectedly during the transition
- **THEN** the acknowledgement reports `UnexpectedTermination` and the group state is `Undefined`
