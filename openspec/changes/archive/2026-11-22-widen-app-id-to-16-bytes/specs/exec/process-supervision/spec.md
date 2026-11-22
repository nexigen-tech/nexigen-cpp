# Spec Delta

## MODIFIED Requirements

### Requirement: Child environment and CPU affinity are configurable

The launch contract SHALL accept optional per-child environment entries, an optional `CpuSet`, an
application name delivered as a `--app-name` argument in the child argv, and an application
identifier delivered as a `--app-id` argument in the same argv. It SHALL apply the environment and
affinity in the child after the inherited environment/affinity reset and before `exec`, and SHALL
reject an out-of-range or duplicate CPU index with `InvalidArgument` before `fork`. Identity
variables set by the execution_manager SHALL take precedence over caller-supplied environment
entries; the application name and identifier SHALL be delivered through argv rather than the
`NEXIGEN_APP_NAME` or `NEXIGEN_APP_ID` environment variables. ExecutionManager SHALL NOT select a
scheduling policy or priority for the application.

#### Scenario: Environment entries are visible to the child

- **WHEN** the caller supplies environment entries at launch
- **THEN** the child observes those names and values in its environment

#### Scenario: ExecutionManager identity takes precedence

- **WHEN** a caller-supplied entry uses a `NEXIGEN_*` identity name
- **THEN** the child observes the value assigned by the execution_manager

#### Scenario: Application name is passed via argv

- **WHEN** the execution_manager launches a child with a resolved application name
- **THEN** the child's argv contains `--app-name <name>` and its environment does not contain
  `NEXIGEN_APP_NAME`

#### Scenario: Application identifier is passed via argv

- **WHEN** the execution_manager launches a child with a resolved identifier
- **THEN** the child's argv contains `--app-id <id>` and its environment does not contain
  `NEXIGEN_APP_ID`

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
- **THEN** the child receives the execution_manager's normal full online-CPU reset rather than inheriting the execution_manager control pin
