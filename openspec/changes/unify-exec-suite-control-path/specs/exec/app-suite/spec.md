# Spec Delta

## MODIFIED Requirements

### Requirement: Function Group initial state

Each Function Group SHALL declare an optional `initial_state` naming a declared state. The group still starts in `Off`; at suite start the supervisor SHALL request `initial_state` through the control host and the shared transition engine. Omitted `initial_state` SHALL default to the first non-`Off` state; a group with only `Off` remains idle. Unknown states SHALL be rejected.

#### Scenario: Suite start enters the configured state

- **WHEN** the suite starts and `ADAS` declares `initial_state: "Running"`
- **THEN** the supervisor requests `ADAS:Running` through the transition engine, which starts the configured processes and confirms readiness

#### Scenario: Initial-state transition failure is visible

- **WHEN** the initial-state transition exhausts a startup budget
- **THEN** the group becomes `Undefined` and the suite reports the failed transition

#### Scenario: Default initial state is the first non-Off state

- **WHEN** a Function Group declares `Off`, `Starting`, and `Running` and omits `initial_state`
- **THEN** the supervisor uses `Starting` as the suite initial state

#### Scenario: Unknown initial state is rejected

- **WHEN** a Function Group declares `initial_state` outside its declared states
- **THEN** manifest validation rejects the group