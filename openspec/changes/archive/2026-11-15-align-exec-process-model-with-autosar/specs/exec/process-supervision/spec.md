# Spec Delta

## MODIFIED Requirements

### Requirement: Dependency-ordered startup

Applications SHALL declare execution dependencies that name a process and the required process state, limited to `Running` or `Terminated`. A dependency and its dependent SHALL belong to the same Function Group State; a dependency on a process outside the requested state, a mutual dependency, or a cycle SHALL be rejected at manifest validation. The `nexigen-em`/`StateClient` control path SHALL start a process only after every dependency has reached its required state during the current transition; the suite supervisor’s readiness inbox is the follow-up `unify-exec-suite-control-path` change. Both paths SHALL terminate processes in reverse dependency order.

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

### Requirement: Startup-timeout recovery policy

When a process exceeds the startup timeout of its startup configuration, the EM SHALL log the failure, retry up to the configured restart attempts with the configured delay, and hand the failure to the transition when the budget is exhausted. A termination timeout SHALL force-terminate the process and let the transition continue.

#### Scenario: Timed-out process is logged and recovered

- **WHEN** a process fails to report `Running` within `startup_timeout` and is SIGKILLed
- **THEN** the EM logs the failure and applies the configured restart policy

#### Scenario: Exhausted startup budget fails the transition

- **WHEN** the last configured startup attempt also times out
- **THEN** the process is force-terminated and the transition reports failure

#### Scenario: Termination timeout continues

- **WHEN** a process does not exit within its termination timeout
- **THEN** the EM force-terminates it and continues the transition


