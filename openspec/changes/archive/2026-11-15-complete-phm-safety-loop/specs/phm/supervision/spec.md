# Spec Delta

## MODIFIED Requirements

### Requirement: Status aggregation

The daemon SHALL aggregate Alive, Logical, and Deadline results per process into a `SupervisionStatus` machine with the states `Ok`, `Failed`, `Expired`, and `Exhausted`; a single failure within `failed_supervision_cycles_tolerance` SHALL yield `Failed`, and consecutive failures beyond the tolerance SHALL yield `Expired`. `Exhausted` SHALL mean that recovery budget is spent, not that the process was stopped deliberately.

#### Scenario: Tolerance defers expiration

- **WHEN** a process fails one cycle within the tolerance
- **THEN** its status becomes `Failed`, not `Expired`

#### Scenario: Repeated failures expire the process

- **WHEN** a process fails more cycles in a row than the tolerance allows
- **THEN** its status becomes `Expired`, triggering recovery

#### Scenario: Deliberate stop is not exhaustion

- **WHEN** the Execution Manager reports that a process is terminating or terminated and its recovery budget is intact
- **THEN** the process is suspended rather than reported `Exhausted`

### Requirement: Lifecycle-armed supervision

The PHM supervisor SHALL accept EM execution-state notifications for each supervised process. On `Running` it SHALL arm Alive/Logical/Deadline supervision and reset the failure counters; on `Terminating` and `Terminated` it SHALL suspend supervision for that process. A `Failed` notification SHALL place the process in `Exhausted` and SHALL NOT restart it again. A suspended process SHALL NOT become `Expired`, SHALL keep its last non-failure status, and SHALL NOT trigger a recovery action. A process that never receives a lifecycle notification SHALL keep the pre-change supervision behavior.

#### Scenario: Running arms supervision

- **WHEN** PHM receives `Running` for a supervised process
- **THEN** Alive and Deadline supervision start for it and the failure counters reset

#### Scenario: Terminating suspends supervision

- **WHEN** PHM receives `Terminating` while the process is within its Alive window
- **THEN** the process is not reported `Expired` and no recovery action is applied

#### Scenario: Terminated does not trigger recovery

- **WHEN** PHM receives `Terminated` for a process and its Alive window later elapses
- **THEN** the supervisor keeps the process suspended and the recovery handler is not invoked

#### Scenario: Failed exhaustion is terminal

- **WHEN** PHM receives `Failed` for a process whose restart budget is spent
- **THEN** the process becomes `Exhausted`, no further restart is requested, and the escalation path runs once

#### Scenario: Legacy PHM keeps working

- **WHEN** no lifecycle event is ever received for a supervised process
- **THEN** Alive, Logical, and Deadline supervision behave exactly as before

## ADDED Requirements

### Requirement: Exhaustion escalates instead of restarting

An `Exhausted` process SHALL NOT trigger another restart attempt. The daemon SHALL escalate it once to the State Manager as a platform fault and SHALL keep the status `Exhausted` until the process reports `Running` again.

#### Scenario: Exhausted process is not restarted again

- **WHEN** a process is `Exhausted` and its Alive window elapses again
- **THEN** the daemon sends no restart request and repeats no escalation

#### Scenario: Recovery returns the process to Ok

- **WHEN** an `Exhausted` process later reports `Running`
- **THEN** its status becomes `Ok` and supervision is armed again

### Requirement: Supervision status vocabulary

`nexigen::phm::SupervisionStatus` SHALL expose exactly the values `Ok`, `Failed`, `Expired`, and `Exhausted`, each with a canonical name, and the full project conversion set.

#### Scenario: Renamed value round-trips

- **WHEN** each `SupervisionStatus` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips, `Exhausted` is reported as `Exhausted`, and no `Stopped` name is produced
