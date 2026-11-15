# Spec Delta

## ADDED Requirements

### Requirement: Suite manifest source format

The suite manifest SHALL be loadable from the existing JSON schema or from the
documented ARXML execution-manifest profile. Both sources SHALL produce the
same `SuiteManifest` value and SHALL be validated by the same rules before any
process starts.

#### Scenario: JSON source is unchanged

- **WHEN** a suite loads from JSON
- **THEN** every existing field and validation rule behaves as before

#### Scenario: ARXML source is normalized

- **WHEN** a suite loads from the ARXML profile
- **THEN** the in-memory manifest, artifacts, and launch behavior match the equivalent JSON manifest
