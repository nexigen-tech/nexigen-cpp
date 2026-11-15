# Spec Delta

## ADDED Requirements

### Requirement: Platform-control manifest fields

The suite manifest SHALL accept optional `resource_groups`,
`trusted_platform`, and `function_group_port_mapping` sections, and each
application SHALL accept optional `resource_group` and `executable_digest`
fields. All new fields SHALL round-trip through `to_json`/`from_json`, default
to "disabled/empty" when absent, and leave every existing manifest valid.

#### Scenario: New fields round-trip

- **WHEN** a manifest with a resource group, trusted policy, port mapping, and per-app fields is serialized and parsed back
- **THEN** every new value is preserved

#### Scenario: Existing manifest stays valid

- **WHEN** a manifest omits every new field
- **THEN** validation accepts it and the EM keeps the previous launch behavior

### Requirement: Resource group validation

Validation SHALL reject duplicate resource-group names, unsafe names, a
non-positive memory limit when declared, a working folder that is not absolute
or under the run root, and an application reference to an undeclared group.

#### Scenario: Duplicate resource group is rejected

- **WHEN** two resource groups share a name
- **THEN** manifest validation rejects the suite

#### Scenario: Undeclared group reference is rejected

- **WHEN** an application names a resource group that is not declared
- **THEN** manifest validation rejects the suite

### Requirement: Trusted and mapping validation

When the trusted policy is enabled, validation SHALL require an absolute command
and a valid SHA-256 digest for every application. Port-mapping entries SHALL
name a declared Function Group and non-empty safe requester ids.

#### Scenario: Trusted policy rejects a relative command

- **WHEN** the trusted policy is enabled and an application uses a relative command
- **THEN** manifest validation rejects the suite

#### Scenario: Port mapping to an unknown group is rejected

- **WHEN** a port mapping names a Function Group the manifest does not declare
- **THEN** manifest validation rejects the suite
