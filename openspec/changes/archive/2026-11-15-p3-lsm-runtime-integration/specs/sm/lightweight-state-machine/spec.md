# Spec Delta

## ADDED Requirements

### Requirement: LSM configuration names its Function Group

`LightweightStateMachineConfig` SHALL include a `function_group` string in its
JSON round-trip and diagnostics.

#### Scenario: Function Group round-trips

- **WHEN** a configuration with `function_group` is serialized and parsed back
- **THEN** the group name is preserved

#### Scenario: Missing group fails validation

- **WHEN** a state machine omits `function_group`
- **THEN** manifest validation rejects it
