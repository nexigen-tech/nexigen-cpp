# Spec Delta

## ADDED Requirements

### Requirement: Lifecycle-armed supervision

The PHM supervisor SHALL accept EM execution-state notifications for each supervised process. On `Running` it SHALL arm Alive/Logical/Deadline supervision and reset the failure counters; on `Terminating` and `Terminated` it SHALL suspend supervision for that process. A suspended process SHALL NOT become `Expired`, SHALL keep its last non-failure status, and SHALL NOT trigger a recovery action. A process that never receives a lifecycle notification SHALL keep the pre-change supervision behavior.

#### Scenario: Running arms supervision

- **WHEN** PHM receives `Running` for a supervised process
- **THEN** Alive and Deadline supervision start for it and the failure counters reset

#### Scenario: Terminating suspends supervision

- **WHEN** PHM receives `Terminating` while the process is within its Alive window
- **THEN** the process is not reported `Expired` and no recovery action is applied

#### Scenario: Terminated does not trigger recovery

- **WHEN** PHM receives `Terminated` for a process and its Alive window later elapses
- **THEN** the supervisor keeps the process suspended and the recovery handler is not invoked

#### Scenario: Legacy PHM keeps working

- **WHEN** no lifecycle event is ever received for a supervised process
- **THEN** Alive, Logical, and Deadline supervision behave exactly as before

### Requirement: Lifecycle channel diagnostics

The PHM daemon SHALL expose the lifecycle channel state through its existing cold-path diagnostics, naming the standard service and the number of lifecycle notifications applied.

#### Scenario: Lifecycle diagnostics are readable

- **WHEN** the daemon renders its diagnostics after applying lifecycle events
- **THEN** the text names `Nexigen/PHM/ExecutionState` and the applied notification count
