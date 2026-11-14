# Spec Delta

## ADDED Requirements

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