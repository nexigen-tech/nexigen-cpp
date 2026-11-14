# exec/process-types Specification

## Purpose

Defines the Execution Manager's process execution model: the `daemon`/`oneshot` termination behavior a process declares, the readiness-reporting contract, the `Unrecoverable State` the manager enters on an unresolvable failure, and the condition under which a Function Group state change is confirmed.

## Requirements

### Requirement: Process termination behavior is declared

A process manifest SHALL declare a termination behavior of `daemon` (continuous) or `oneshot` (transient). A `daemon` SHALL run for the whole duration of every phase it is bound to; a `oneshot` SHALL self-terminate, and exiting with status 0 SHALL be treated as success rather than a failure.

#### Scenario: Oneshot exiting zero is success

- **WHEN** a `oneshot` process terminates itself with exit status 0
- **THEN** the manager records a normal termination and does not run the failure path

#### Scenario: Daemon expected to stay up

- **WHEN** a `daemon` process is bound to an active phase
- **THEN** the manager treats it as expected to remain running for that phase's duration

### Requirement: Daemon readiness is reported

A `daemon` process SHALL report its readiness (`Run`) to the Execution Manager once its own initialization is complete. A process that does not report readiness SHALL remain in its starting state; a `oneshot` process need not report readiness.

#### Scenario: Daemon reports readiness before serving

- **WHEN** a `daemon` finishes its own initialization and reports `Run`
- **THEN** the manager moves it to the running state and considers it available for the phase

#### Scenario: Daemon terminating before readiness is a failure

- **WHEN** a `daemon` terminates before it has reported `Run`
- **THEN** the manager records an unexpected self-termination and runs the failure path

### Requirement: Unexpected self-termination is a failure

The Execution Manager SHALL treat the following as unexpected self-termination: a `daemon` terminating without a prior stop request, a `daemon` terminating before reporting readiness, or any process terminating with a non-zero status. Each SHALL trigger the failure path rather than being ignored.

#### Scenario: Daemon dies mid-phase

- **WHEN** a `daemon` bound to an active phase terminates without a stop request
- **THEN** the manager records the event and initiates failure handling

#### Scenario: Non-zero exit is a failure

- **WHEN** any process terminates with a non-zero status
- **THEN** the manager records an unexpected termination and initiates failure handling

### Requirement: Unrecoverable State stops all activity

When the Execution Manager encounters a situation it cannot resolve, it SHALL enter an `Unrecoverable State`: it SHALL stop taking further actions, terminate every process it manages, and expose a project-specific handling hook.

#### Scenario: Manager enters Unrecoverable State

- **WHEN** a failure occurs that the manager cannot resolve
- **THEN** the manager stops acting, terminates all managed processes, and invokes the project-specific handling

### Requirement: State change is confirmed by readiness and completion

A Function Group state change to `Run` SHALL be confirmed only when every `daemon` bound to that state has reported readiness and every `oneshot` bound to it has terminated successfully. A `daemon` that failed to report readiness or a `oneshot` that terminated with a non-zero status SHALL prevent confirmation.

#### Scenario: All processes ready confirms the switch

- **WHEN** every bound `daemon` has reported `Run` and every bound `oneshot` has exited 0
- **THEN** the manager confirms the Function Group state change

#### Scenario: An unready daemon blocks confirmation

- **WHEN** a bound `daemon` has not reported readiness within its startup timeout
- **THEN** the manager does not confirm the state change and reports the failure
