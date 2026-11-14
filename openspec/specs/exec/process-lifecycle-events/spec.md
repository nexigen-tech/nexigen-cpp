# exec/process-lifecycle-events Specification

## Purpose
TBD - created by archiving change em-phm-lifecycle-events. Update Purpose after archive.

## Requirements

### Requirement: Process lifecycle vocabulary

`nexigen::exec` SHALL expose a `ProcessLifecycleState` scoped enum with the values `Running`, `Terminating`, and `Terminated` and the full project conversion set (`to_string`/`from_string`, ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter`).

#### Scenario: Lifecycle state round-trips

- **WHEN** each `ProcessLifecycleState` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders the name

### Requirement: Process lifecycle notification

`nexigen::exec` SHALL expose a fixed-size, trivially copyable `ProcessLifecycleEvent` carrying the application name and `ProcessLifecycleState`. It SHALL provide a cold-path `to_string()` member plus free helper, ADL `to_json`/`from_json`, and a `std::formatter` specialization, and it SHALL be usable as a `com::concepts::Message` without serialization or heap allocation.

#### Scenario: Lifecycle event round-trips

- **WHEN** an event is passed through `to_string`, `std::format`, `to_json`, and `from_json`
- **THEN** the application name and state are preserved

#### Scenario: Hostile JSON leaves the event unchanged

- **WHEN** `from_json` receives a non-object, a missing field, or an unknown state
- **THEN** the destination event keeps its previous value

### Requirement: EM publishes process lifecycle

When the COM control host is active, the EM SHALL publish a `ProcessLifecycleEvent` on `Nexigen/PHM/ExecutionState` and ring `Nexigen/PHM/ExecutionStateEvent` when the process table moves a process to `Running`, when a stop starts (`Terminating`), and when the process is reaped (`Terminated`). A restart SHALL publish `Running` again, and publishing SHALL succeed when no PHM subscriber is present.

#### Scenario: Readiness publishes Running

- **WHEN** a reporting daemon reports readiness and the tracker records `Running`
- **THEN** the control host publishes `Running` for that application

#### Scenario: Stop publishes Terminating then Terminated

- **WHEN** the transition engine stops a running application and the reaper records its exit
- **THEN** the control host publishes `Terminating` before the suite stop call and `Terminated` after the reap

#### Scenario: No PHM subscriber is harmless

- **WHEN** the control host publishes lifecycle samples and no PHM daemon is running
- **THEN** the publication succeeds and suite control continues unaffected

### Requirement: Lifecycle event performance

Lifecycle publication SHALL occur only on process state transitions, never in a cyclic application hot path, and SHALL use the fixed-size POD directly without allocating, serializing, or locking.

#### Scenario: Steady state stays quiet

- **WHEN** every process remains in one lifecycle state
- **THEN** the control host publishes no further lifecycle samples
