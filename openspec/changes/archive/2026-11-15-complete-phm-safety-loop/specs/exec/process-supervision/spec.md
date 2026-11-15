# Spec Delta

## ADDED Requirements

### Requirement: Recovery restart is admitted asynchronously

A restart requested over the PHM recovery channel SHALL be answered before the restart is performed. The Execution Manager SHALL validate the request, mark the process as terminating, enqueue the restart, and answer `Accepted` — or `Rejected` with a reason — without waiting for the stop or the launch. The restart itself SHALL run later on the control thread through the same stop-then-launch sequence as any other restart.

#### Scenario: Answer arrives before the process stops

- **WHEN** a recovery restart is requested for a running process that needs the full termination grace period
- **THEN** the answer arrives immediately and the stop and relaunch happen afterwards

#### Scenario: Rejection is answered immediately

- **WHEN** a recovery restart names an unknown process or the suite is not operational
- **THEN** the answer is `Rejected` with the corresponding reason and no process is touched

#### Scenario: Accepted restart still publishes lifecycle

- **WHEN** an admitted restart later stops and relaunches the process
- **THEN** `Terminating`, `Terminated`, and `Running` are published for it exactly as for an ordinary restart

### Requirement: Recovery restart consumes the restart budget

A restart requested over the PHM recovery channel SHALL count against the process's configured restart budget instead of bypassing it. When the last permitted attempt also fails to reach `Running`, the Execution Manager SHALL publish `Failed` and stop attempting.

#### Scenario: Each recovery restart spends one attempt

- **WHEN** a process is restarted on PHM request
- **THEN** the configured restart budget decreases by one

#### Scenario: Exhausted budget reports Failed

- **WHEN** the last permitted recovery restart also fails to reach `Running`
- **THEN** the Execution Manager publishes `Failed` for that process and performs no further launch

#### Scenario: Budget exhaustion is not a crash retry

- **WHEN** the restart budget is already spent
- **THEN** a further recovery restart request is answered `Rejected` with `AlreadyRestarting` or a budget reason, and no launch is attempted

### Requirement: Recovery response carries a rejection reason

The recovery answer SHALL carry the request correlation id, the outcome, and a reason. The outcome SHALL be `Accepted`, `InProgress`, or `Rejected`. A rejected answer SHALL name one of `UnknownProcess`, `NotOperational`, `AlreadyRestarting`, `InvalidAction`, or `SystemError`, never `None`.

#### Scenario: Accepted answer echoes the request id

- **WHEN** a restart request carrying a correlation id is admitted
- **THEN** the answer repeats that id with outcome `Accepted` and reason `None`

#### Scenario: Not-operational suite rejects

- **WHEN** a restart is requested while the suite is not in an operational state
- **THEN** the answer is `Rejected` with reason `NotOperational`

#### Scenario: Unsupported action rejects

- **WHEN** a request names an action this channel does not serve
- **THEN** the answer is `Rejected` with reason `InvalidAction` and the watchdog is left to PHM
