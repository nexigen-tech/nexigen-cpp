# Spec Delta

## MODIFIED Requirements

### Requirement: Process lifecycle vocabulary

`nexigen::exec` SHALL expose a `ProcessLifecycleState` scoped enum with the values `Running`, `Terminating`, `Terminated`, and `Failed` and the full project conversion set (`to_string`/`from_string`, ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter`). `Failed` SHALL mean that the restart budget for the process is spent and the Execution Manager will not launch it again on its own.

#### Scenario: Lifecycle state round-trips

- **WHEN** each `ProcessLifecycleState` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders the name

#### Scenario: Failed is distinguished from Terminated

- **WHEN** a `Failed` state passes through `to_string` and `from_json`
- **THEN** it renders and parses as `Failed`, not as `Terminated`

### Requirement: Process lifecycle notification

`nexigen::exec` SHALL expose a fixed-size, trivially copyable `ProcessLifecycleEvent` carrying the application name, its Function Group, and `ProcessLifecycleState`. It SHALL provide a cold-path `to_string()` member plus free helper, ADL `to_json`/`from_json`, and a `std::formatter` specialization, and it SHALL be usable as a `com::concepts::Message` without serialization or heap allocation.

#### Scenario: Lifecycle event round-trips

- **WHEN** an event is passed through `to_string`, `std::format`, `to_json`, and `from_json`
- **THEN** the application name, the Function Group, and the state are preserved

#### Scenario: Hostile JSON leaves the event unchanged

- **WHEN** `from_json` receives a non-object, a missing field, or an unknown state
- **THEN** the destination event keeps its previous value

### Requirement: EM publishes process lifecycle

When the COM control host is active, the EM SHALL publish a `ProcessLifecycleEvent` on `Nexigen/PHM/ExecutionState` and ring `Nexigen/PHM/ExecutionStateEvent` when the process table moves a process to `Running`, when a stop starts (`Terminating`), and when the process is reaped (`Terminated`). It SHALL publish `Failed` once, when the restart budget for the process is exhausted, and SHALL NOT publish `Running` afterwards until a later successful launch. A restart SHALL publish `Running` again, and publishing SHALL succeed when no PHM subscriber is present.

#### Scenario: Readiness publishes Running

- **WHEN** a reporting daemon reports readiness and the tracker records `Running`
- **THEN** the control host publishes `Running` for that application with its Function Group

#### Scenario: Stop publishes Terminating then Terminated

- **WHEN** the transition engine stops a running application and the reaper records its exit
- **THEN** the control host publishes `Terminating` before the suite stop call and `Terminated` after the reap

#### Scenario: Exhausted budget publishes Failed once

- **WHEN** the last permitted restart attempt also fails to reach `Running`
- **THEN** the control host publishes `Failed` for that application exactly once and starts no further attempt

#### Scenario: No PHM subscriber is harmless

- **WHEN** the control host publishes lifecycle samples and no PHM daemon is running
- **THEN** the publication succeeds and suite control continues unaffected
