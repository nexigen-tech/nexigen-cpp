# Spec Delta

## ADDED Requirements

### Requirement: Optional machine cross-check before launch

When a Machine Manifest is supplied, `nexigen-exec start` and `nexigen-em`
SHALL load it and cross-check the Execution Manifest before creating the suite
supervisor. A failed machine load or cross-check SHALL fail visibly and SHALL
fork no process. Omitting the machine manifest SHALL preserve the previous
behavior.

#### Scenario: Matching machine starts

- **WHEN** both manifests agree on every application's process, command, and Function Group
- **THEN** the suite starts normally

#### Scenario: Mismatch refuses startup

- **WHEN** the machine cross-check fails
- **THEN** the CLI reports a failure and no suite supervisor is created
