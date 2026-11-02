# Spec Delta

## MODIFIED Requirements

### Requirement: Child environment and CPU affinity are configurable

The launch contract SHALL accept optional per-child environment entries, an optional allowed-CPU list, and an application name delivered as a `--app-name` argument in the child argv. It SHALL apply the environment and affinity in the child after the inherited environment and before `exec`, and SHALL reject an out-of-range CPU index with `InvalidArgument` before `fork`. Identity variables set by the runner SHALL take precedence over caller-supplied environment entries; the application name SHALL be delivered through argv rather than the `NEXIGEN_APP_NAME` environment variable.

#### Scenario: Environment entries are visible to the child

- **WHEN** the caller supplies environment entries at launch
- **THEN** the child observes those names and values in its environment

#### Scenario: Runner identity takes precedence

- **WHEN** a caller-supplied entry uses a `NEXIGEN_*` identity name
- **THEN** the child observes the value assigned by the runner

#### Scenario: Application name is passed via argv

- **WHEN** the runner launches a child with a resolved application name
- **THEN** the child's argv contains `--app-name <name>` and its environment does not contain `NEXIGEN_APP_NAME`

#### Scenario: CPU affinity restricts the child

- **WHEN** the caller supplies an allowed-CPU list
- **THEN** the child's allowed CPU set equals that list

#### Scenario: Invalid CPU index is rejected before fork

- **WHEN** the allowed-CPU list contains an index outside the online set
- **THEN** the launch returns `InvalidArgument` without creating a child process
