# Spec Delta

## ADDED Requirements

### Requirement: Recovery acknowledgement timeout configuration

The health manifest SHALL accept an optional `recovery_timeout_ms` at the top level, defining how long the daemon waits for a State Manager acknowledgement after escalating a fault. An absent field SHALL default to 1000 ms; a value of `0` SHALL be rejected when the manifest is loaded, because it would make every escalation fail instantly.

#### Scenario: Configured timeout is parsed

- **WHEN** a manifest declares `recovery_timeout_ms`
- **THEN** the daemon uses that value for every escalation and renders it in its diagnostics

#### Scenario: Absent field takes the default

- **WHEN** a manifest omits `recovery_timeout_ms`
- **THEN** the daemon waits 1000 ms for each acknowledgement

#### Scenario: Zero timeout is rejected

- **WHEN** a manifest declares `recovery_timeout_ms` as `0`
- **THEN** manifest validation fails with `InvalidArgument` and no supervision starts

#### Scenario: Timeout round-trips through JSON

- **WHEN** a manifest with `recovery_timeout_ms` is serialized and parsed back
- **THEN** the parsed value equals the original
