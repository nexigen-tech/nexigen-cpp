# Spec Delta

## MODIFIED Requirements

### Requirement: Dependency-ordered startup

Applications SHALL declare execution dependencies that name a process and the required process state, limited to `Running` or `Terminated`. A dependency and its dependent SHALL belong to the same Function Group State; a dependency on a process outside the requested state, a mutual dependency, or a cycle SHALL be rejected at manifest validation. Both the suite supervisor and the `nexigen-em` control path SHALL start a process only after every dependency has reached its required state during the current transition, and both SHALL terminate processes in reverse dependency order. When the dependency is the time-base daemon, the dependent SHALL start only after the daemon is `Running` and the published time base reports `Ready`.

#### Scenario: Dependent waits for its dependency

- **WHEN** application B declares application A as a dependency and A has not reported `Running`
- **THEN** the EM does not start B until A's running notification arrives

#### Scenario: Dependent waits for Terminated

- **WHEN** application D depends on self-terminating C reaching `Terminated`
- **THEN** the EM starts D only after C is reaped

#### Scenario: Termination follows reverse order

- **WHEN** B depends on A reaching `Running` and both are leaving the requested state
- **THEN** the EM terminates B before A

#### Scenario: Cross-state dependency is rejected

- **WHEN** a process in `ADAS:Driving` declares a dependency on a process configured only for `ADAS:Fallback`
- **THEN** manifest validation rejects the configuration

#### Scenario: Mutual dependency is rejected

- **WHEN** A depends on B and B depends on A
- **THEN** manifest validation rejects the cycle

#### Scenario: Missing dependency fails startup

- **WHEN** a dependency never reports `Running` within its startup timeout
- **THEN** the EM fails the dependent's startup with a visible error

#### Scenario: Time-base dependency waits for readiness

- **WHEN** the time-base daemon is `Running` but the published base is not `Ready`
- **THEN** the dependent does not start until the base reports `Ready` or the startup timeout expires

## ADDED Requirements

### Requirement: Time-base loss retry gate

An application that exits with the `time_base_lost` reason SHALL NOT be retried while the time-base daemon is not `Running` with a `Ready` base. The supervisor SHALL hold the retry — without consuming the application's restart budget — and SHALL resume the normal restart policy once the daemon is ready again.

#### Scenario: Retry is held until the daemon returns

- **WHEN** an application exits with `time_base_lost` and the daemon is absent
- **THEN** the supervisor holds the retry, the restart budget is unchanged, and no relaunch is attempted

#### Scenario: Retry resumes after readiness

- **WHEN** the daemon becomes `Running` and the base reports `Ready`
- **THEN** the held retry proceeds under the application's configured restart policy

#### Scenario: Other exit reasons are unaffected

- **WHEN** an application exits for a reason other than `time_base_lost`
- **THEN** the existing restart policy applies without the time-base gate
