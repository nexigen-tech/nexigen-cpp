# Spec Delta

## ADDED Requirements

### Requirement: SM manifest carries lightweight state machines

`SmManifest` SHALL accept an optional `state_machines` array of
`LightweightStateMachineConfig` values. The field SHALL default to empty,
round-trip through `to_json`/`from_json`, and leave every manifest without it
valid and behaviorally unchanged.

#### Scenario: State machines round-trip

- **WHEN** a manifest with one state machine is serialized and parsed back
- **THEN** its states, actions, transitions, and recovery rules are preserved

#### Scenario: Existing manifest remains valid

- **WHEN** a manifest omits `state_machines`
- **THEN** validation succeeds and the SM keeps its existing behavior
