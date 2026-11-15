# sm/lightweight-state-machine Specification

## Purpose
TBD - created by archiving change p3-lightweight-state-machine. Update Purpose after archive.

## Requirements

### Requirement: Lightweight state-machine vocabulary

`nexigen::sm` SHALL expose `LsmActionType` (`None`, `RequestFunctionGroupState`,
`PublishState`), `LsmAction`, `LsmTransition`, `LsmErrorRecovery`, and
`LightweightStateMachineConfig`. Every type SHALL provide the full conversion
set (`to_string`/`from_string` for enums, `to_string()` plus ADL JSON for
structs, and `std::formatter`).

#### Scenario: Action type round-trips

- **WHEN** each `LsmActionType` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders it

#### Scenario: Configuration round-trips

- **WHEN** a configuration with states, actions, transitions, and recovery rules passes through `to_json` and `from_json`
- **THEN** every field is preserved

### Requirement: Transition tables and action lists

`LightweightStateMachine::request(target, requester)` SHALL reject an unknown
target or a missing transition with `InvalidGraph` and leave the state
unchanged. Otherwise it SHALL execute the transition's action list in order
through the `LsmActionSink` and set the target state only after every action
succeeds.

#### Scenario: Actions run in order

- **WHEN** a transition lists `PublishState` then `RequestFunctionGroupState`
- **THEN** the sink observes that exact order and the state becomes the target

#### Scenario: Missing transition is rejected

- **WHEN** the table has no edge from the current state to the requested target
- **THEN** the machine returns `InvalidGraph` and keeps its state

#### Scenario: Failing action stops the transition

- **WHEN** an action sink returns an error
- **THEN** the machine does not set the target state and applies error recovery

### Requirement: Error recovery table

On an action failure, the machine SHALL use the first error-recovery rule whose
`from` matches the failed target or the current state, execute its actions, and
move to its fallback state. No matching rule SHALL leave the state unchanged
and return the original action error.

#### Scenario: Recovery falls back

- **WHEN** an action for `Active` fails and a rule maps `Active` to `Fallback`
- **THEN** the machine executes the fallback actions and reports `Fallback` as current

#### Scenario: No recovery rule is visible

- **WHEN** an action fails and no recovery rule matches
- **THEN** the machine returns the action error without changing state

### Requirement: Action-index validation

Every action index in a transition or recovery rule SHALL be below the action
list size; an out-of-range index SHALL make the configuration invalid.

#### Scenario: Out-of-range action is rejected

- **WHEN** a transition references action index 5 and the action list has two entries
- **THEN** loading the configuration fails with `InvalidArgument`
