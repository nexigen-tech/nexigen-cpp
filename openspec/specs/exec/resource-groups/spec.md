# exec/resource-groups Specification

## Purpose
TBD - created by archiving change p2-exec-platform-controls. Update Purpose after archive.

## Requirements

### Requirement: Resource group vocabulary

`nexigen::exec` SHALL expose a `ResourceGroupConfig` value carrying a name, a
memory limit in mebibytes (`0` = unlimited), a CPU set, and an optional working
folder. It SHALL provide a cold-path `to_string()` member plus free helper, ADL
`to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter`
specialization.

#### Scenario: Resource group round-trips

- **WHEN** a resource group with a memory limit, CPU set, and working folder passes through `to_string`, `std::format`, `to_json`, and `from_json`
- **THEN** every field is preserved and the text/JSON name the group

#### Scenario: Hostile JSON leaves the group unchanged

- **WHEN** `from_json` receives a non-object or a wrong-typed field
- **THEN** the destination group keeps its previous values

### Requirement: Applications resolve one resource group

An application MAY name one resource group in its manifest entry. The EM SHALL
resolve the effective launch values as: app `cpus` when non-empty, otherwise
the group's CPU set; the group's memory limit; the group's working folder.
Validation SHALL reject duplicate or unsafe group names, a reference to an
unknown group, and a working folder outside the allowed absolute/run-root
forms.

#### Scenario: Group defaults apply

- **WHEN** an application references a group with a memory limit and CPU set and declares no CPU override
- **THEN** the launch uses the group's memory limit and CPU set

#### Scenario: Application CPU override wins

- **WHEN** an application references a group with a CPU set and also declares its own non-empty CPU set
- **THEN** the application CPU set is applied and the group CPU set is ignored

#### Scenario: Unknown group is rejected

- **WHEN** an application references a resource group that the manifest does not declare
- **THEN** manifest validation rejects the suite

### Requirement: Resource limits are enforced at launch

Before `exec`, the child SHALL change to the resolved working folder when one is
configured and SHALL apply `RLIMIT_AS` when the memory limit is non-zero. A
failed `chdir` or `setrlimit` SHALL fail the start visibly, and no
`resource.json` SHALL be published for that failed start.

#### Scenario: Memory limit is applied

- **WHEN** an application with a non-zero memory limit starts
- **THEN** the child is limited to that virtual-address-space budget

#### Scenario: Working folder is applied

- **WHEN** an application with a configured working folder starts
- **THEN** the child's current directory is that folder before `exec`

#### Scenario: Limit failure is visible

- **WHEN** `chdir` or `setrlimit` fails in the child
- **THEN** `start` reports a failure and publishes no `resource.json`
