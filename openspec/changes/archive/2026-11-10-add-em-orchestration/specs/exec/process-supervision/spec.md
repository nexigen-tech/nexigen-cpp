# Spec Delta

## ADDED Requirements

### Requirement: Dependency-ordered startup

Applications SHALL declare a `dependencies` list in the manifest, and the EM SHALL start an application only after every named dependency has reported `Running`; a dependent whose dependency never becomes ready SHALL fail startup with a visible error instead of launching.

#### Scenario: Dependent waits for its dependency

- **WHEN** application B declares application A as a dependency and A has not reported `Running`
- **THEN** the EM does not start B until A's running notification arrives

#### Scenario: Missing dependency fails startup

- **WHEN** a dependency never reports `Running` within its startup timeout
- **THEN** the EM fails the dependent's startup with a visible error

### Requirement: Startup-timeout recovery policy

When a process is forcibly terminated after exceeding its `startup_timeout`, the EM SHALL log the failure and hand the process to its configured recovery rule (restart up to `restart_attempts`), rather than leaving it stopped without a record.

#### Scenario: Timed-out process is logged and recovered

- **WHEN** a process fails to report `Running` within `startup_timeout` and is SIGKILLed
- **THEN** the EM logs the failure and applies the configured restart policy

### Requirement: Crash cleanup before restart

Before restarting a process that died abnormally, the EM SHALL run the iceoryx2 cleanup of that process's stale node markers and handles, so the relaunched process registers its services with the same identifiers without `AlreadyExists` conflicts or stuck locks.

#### Scenario: Relaunch reuses the same identifiers

- **WHEN** a crashed process is restarted under its restart policy
- **THEN** the EM cleans the stale iceoryx2 markers first and the process re-registers its services successfully
