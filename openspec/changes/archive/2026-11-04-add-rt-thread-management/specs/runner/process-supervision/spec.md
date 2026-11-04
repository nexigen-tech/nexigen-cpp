# Spec Delta

## MODIFIED Requirements

### Requirement: Child environment and CPU affinity are configurable

The launch contract SHALL accept optional per-child environment entries, an optional `CpuSet`, and an application name delivered as a `--app-name` argument in the child argv. It SHALL apply the environment and affinity in the child after the inherited environment/affinity reset and before `exec`, and SHALL reject an out-of-range or duplicate CPU index with `InvalidArgument` before `fork`. Identity variables set by the runner SHALL take precedence over caller-supplied environment entries; the application name SHALL be delivered through argv rather than the `NEXIGEN_APP_NAME` environment variable. Runner SHALL NOT select a scheduling policy or priority for the application.

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

- **WHEN** the caller supplies a non-empty CPU set
- **THEN** the child's allowed CPU set equals that set

#### Scenario: CPUs above 63 reach the child unchanged

- **WHEN** a valid set contains a permitted CPU above 63
- **THEN** that CPU is included in the pre-`exec` affinity without conversion to a uint64 mask

#### Scenario: Invalid CPU index is rejected before fork

- **WHEN** the CPU set contains an index outside the online set, a duplicate, or an unrepresentable index
- **THEN** the launch returns `InvalidArgument` without creating a child process

#### Scenario: Empty CPU set retains existing reset behavior

- **WHEN** no application CPU set is supplied
- **THEN** the child receives the runner's normal full online-CPU reset rather than inheriting the runner control pin
