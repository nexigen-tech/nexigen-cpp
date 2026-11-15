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

### Requirement: Suite supervisor hosts the StateClient control path

When `NEXIGEN_ENABLE_COM` is enabled, the suite supervisor SHALL host the `StateClient` receiver and the `Nexigen/EM/State/<app>` readiness inbox through the shared control host. A `SetState` received by the suite path SHALL drive the same `FunctionGroupRegistry`, process table, and transition engine as `nexigen-em`; with the gate disabled, the suite supervisor SHALL keep the documented non-reporting fallback and expose no `StateClient`.

#### Scenario: Suite supervisor receives SetState

- **WHEN** the SM sends `SetState` while a suite supervisor is running with COM enabled
- **THEN** the supervisor's `StateClient` validates the command and drives the suite's Function Group transition

#### Scenario: Suite readiness gates confirmation

- **WHEN** a requested suite state includes a reporting daemon that has not reported `Running`
- **THEN** the transition reports `InProgress` and does not set the target state current

#### Scenario: Gate-off suite exposes no StateClient

- **WHEN** the build has `NEXIGEN_ENABLE_COM=OFF`
- **THEN** the suite supervisor uses the documented file-based/non-reporting fallback and does not create a `StateClient`

### Requirement: Process start and stop are delegated to the suite host

`StateClient` SHALL accept optional process start and stop handlers. When handlers are installed, `start_app` and `stop_app` SHALL call them instead of contacting `ExecutionManager` directly, so the suite supervisor keeps ownership of `ChildProcess`, attempt artifacts, and restart policy. Without handlers, the existing direct `ExecutionManager` path SHALL remain.

#### Scenario: Start handler launches the supervised process

- **WHEN** a transition starts an application through the suite control host
- **THEN** the handler launches the application with the suite attempt/artifact layout and `StateClient` waits for its readiness

#### Scenario: Stop handler uses the suite stop path

- **WHEN** a transition stops an application through the suite control host
- **THEN** the handler stops the supervised process and the tracker records `Terminating` then `Terminated`

#### Scenario: Direct path remains for nexigen-em

- **WHEN** no handlers are installed
- **THEN** `StateClient` continues to start and stop applications through `ExecutionManager`

### Requirement: AccessDenied transition result

`exec::StateResult` SHALL gain the value `AccessDenied` with the full project
conversion set (`to_string`/`from_string`, ADL `to_json`/`from_json` under
`JSON_NOEXCEPTION=1`, and `std::formatter`). The value SHALL be append-only so
existing results keep their numeric values.

#### Scenario: AccessDenied round-trips

- **WHEN** `AccessDenied` passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** the canonical name round-trips and the formatter renders it

#### Scenario: Existing results keep their values

- **WHEN** the enum table is inspected after the addition
- **THEN** every pre-existing result retains its original numeric value and spelling
