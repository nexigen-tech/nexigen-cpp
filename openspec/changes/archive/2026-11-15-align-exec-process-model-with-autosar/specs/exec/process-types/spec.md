# Spec Delta

## MODIFIED Requirements

### Requirement: Daemon readiness is reported

Reporting and non-reporting behavior SHALL be declared independently of the `daemon`/`oneshot` termination behavior. A reporting process SHALL report its readiness (`Run`) to the Execution Manager once its own initialization is complete and SHALL remain in `Starting` until it does; a non-reporting process SHALL enter `Running` implicitly after creation. A report from a non-reporting process SHALL be rejected as a violation. This contract is enforced by the `nexigen-em`/`StateClient` path; suite-supervisor readiness is the follow-up `unify-exec-suite-control-path` change.

#### Scenario: Daemon reports readiness before serving

- **WHEN** a reporting `daemon` finishes its initialization and reports `Run`
- **THEN** the manager moves it from `Starting` to `Running` and considers it available for the phase

#### Scenario: Non-reporting daemon starts running

- **WHEN** a non-reporting `daemon` is created successfully
- **THEN** the manager moves it to `Running` without waiting for a report

#### Scenario: Non-reporting process report is rejected

- **WHEN** a non-reporting process calls the readiness report API
- **THEN** the manager rejects the report, logs the violation, and does not change its state

#### Scenario: Daemon terminating before readiness is a failure

- **WHEN** a reporting `daemon` terminates while still in `Starting`
- **THEN** the manager records an unexpected self-termination and runs the failure path

## ADDED Requirements

### Requirement: Process lifecycle states

The Execution Manager SHALL track each process through `Idle`, `Starting`, `Running`, `Terminating`, and `Terminated`. A reporting process SHALL enter `Running` only after its readiness report; a non-reporting process SHALL enter `Running` after creation. A termination request SHALL move the process to `Terminating`, and reaping SHALL move it to `Terminated`. The process-state vocabulary SHALL round-trip through text, formatting, and JSON.

#### Scenario: Reporting process lifecycle

- **WHEN** a reporting process is created, reports `Run`, is asked to stop, and is reaped
- **THEN** the manager observes `Starting` -> `Running` -> `Terminating` -> `Terminated`

#### Scenario: Non-reporting process lifecycle

- **WHEN** a non-reporting process is created, is asked to stop, and is reaped
- **THEN** the manager observes `Starting` -> `Running` -> `Terminating` -> `Terminated` without a report

#### Scenario: Process state vocabulary round-trips

- **WHEN** each process-state value passes through `to_string`, `from_string`, `to_json`, `from_json`, and `std::format`
- **THEN** every canonical name round-trips and the formatter renders the name
