# Spec Delta

## MODIFIED Requirements

### Requirement: Function Group configuration

The EM SHALL support Function Groups declared in its configuration, each with its own set of states. Every application SHALL reference exactly one Function Group and SHALL declare one or more state-dependent startup configurations for states of that group; a group state switch SHALL start or stop exactly the processes whose configuration selects the requested state. `Off` SHALL map no processes, and a configuration that references `Off` or a state of another Function Group SHALL be rejected.

#### Scenario: Group switch stops its processes

- **WHEN** the group `ADAS` transitions to `Off`
- **THEN** every process whose configuration selects the previous `ADAS` state is stopped, and processes of other groups keep running

#### Scenario: Group switch starts its processes

- **WHEN** the group `ADAS` transitions from `Off` to `Running`
- **THEN** exactly the processes configured for `ADAS:Running` are started

#### Scenario: Process unchanged between states keeps running

- **WHEN** a process is configured for both `ADAS:Running` and `ADAS:Fallback` with an identical startup configuration
- **THEN** the transition does not stop or restart it

#### Scenario: Cross-group state is rejected

- **WHEN** a process of group `ADAS` declares a startup configuration for `Vehicle:Driving`
- **THEN** manifest validation rejects the configuration

## ADDED Requirements

### Requirement: State-dependent startup configuration

Each application SHALL carry a non-empty set of startup configurations, each naming a state of its Function Group and providing arguments, environment variables, a startup timeout, a termination timeout, a restart attempt count, and a restart delay. The same state SHALL NOT appear twice for one application, and every configuration SHALL round-trip through the manifest JSON and the diagnostic text/formatting conversion set.

#### Scenario: Startup configuration round-trips

- **WHEN** an application's startup configurations pass through `to_json`, `from_json`, `to_string`, and `std::format`
- **THEN** every state, argument, environment entry, timeout, and restart budget is preserved

#### Scenario: Duplicate state is rejected

- **WHEN** one application declares two startup configurations for `ADAS:Running`
- **THEN** manifest validation rejects the application

#### Scenario: Off startup configuration is rejected

- **WHEN** an application declares a startup configuration for `ADAS:Off`
- **THEN** manifest validation rejects the application

### Requirement: Function Group initial state

Each Function Group SHALL declare an optional `initial_state` naming a declared
state. The group still starts in `Off`; at suite start the supervisor SHALL
start the processes configured for `initial_state`. Omitted `initial_state`
SHALL default to the first non-`Off` state; a group with only `Off` stays idle.
Unknown states SHALL be rejected.

#### Scenario: Suite start enters the configured state

- **WHEN** the suite starts and `ADAS` declares `initial_state: "Running"`
- **THEN** the supervisor starts the processes configured for `ADAS:Running`

#### Scenario: Default initial state is the first non-Off state

- **WHEN** a Function Group declares `Off`, `Starting`, and `Running` and omits `initial_state`
- **THEN** the supervisor uses `Starting` as the suite initial state

#### Scenario: Unknown initial state is rejected

- **WHEN** a Function Group declares `initial_state` outside its declared states
- **THEN** manifest validation rejects the group
