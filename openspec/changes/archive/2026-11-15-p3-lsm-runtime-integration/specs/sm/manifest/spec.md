# Spec Delta

## ADDED Requirements

### Requirement: LSM manifest validation

`SmManifest::validate` SHALL reject an LSM binding to an unknown Function Group,
a duplicate binding for one group, and an LSM state that the group does not
declare. Manifests without `state_machines` SHALL remain valid.

#### Scenario: Unknown group is rejected

- **WHEN** an LSM names a Function Group absent from `function_groups`
- **THEN** validation fails

#### Scenario: Existing manifest remains valid

- **WHEN** a manifest omits `state_machines`
- **THEN** validation succeeds
