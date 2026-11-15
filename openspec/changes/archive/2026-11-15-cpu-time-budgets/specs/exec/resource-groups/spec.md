# Spec Delta

## ADDED Requirements

### Requirement: Group cgroups are prepared before launch

A resource group SHALL accept `cpu_quota_us`, `cpu_period_us` (default 100000),
and `memory_max_mb`; the manifest SHALL accept an optional absolute `cgroup_root`
(default `/sys/fs/cgroup`). For every budgeted group the manager SHALL create
`<root>/nexigen/<suite>/<group>` and write `cpu.max` as `"<quota> <period>"` and
`memory.max` as a byte count, failing the start with `SystemError` when the
hierarchy is unavailable.

#### Scenario: Budgeted group becomes a cgroup

- **WHEN** a group declares `cpu_quota_us = 50000` and `cpu_period_us = 100000`
- **THEN** the group directory exists and `cpu.max` contains `50000 100000`

#### Scenario: An unavailable hierarchy fails the start

- **WHEN** a budget is configured and the cgroup directory cannot be created
- **THEN** the start fails with `SystemError` and no application is launched

#### Scenario: Groups without budgets are untouched

- **WHEN** no group declares a CPU or cgroup-memory budget
- **THEN** no cgroup path is created and the previous behavior is preserved

### Requirement: The child joins its group cgroup

The child SHALL move itself into its group cgroup by writing `0` to
`cgroup.procs` before `exec`, after the other privileged setup. A failed join
SHALL report a setup failure and start no application.

#### Scenario: The child joins the group cgroup

- **WHEN** an application in a budgeted group starts
- **THEN** `0` is written to the group's `cgroup.procs` before `exec`

#### Scenario: A failed join starts nothing

- **WHEN** `cgroup.procs` cannot be written
- **THEN** the setup handshake reports a failure and the application does not start

### Requirement: Budget validation

Manifest validation SHALL reject a relative `cgroup_root`, a `cpu_period_us`
outside 1000–1000000 when a quota is set, and any group whose CPU or memory
budget cannot be represented.

#### Scenario: A relative cgroup root is rejected

- **WHEN** the manifest sets `cgroup_root` to a relative path
- **THEN** validation fails with `InvalidArgument`

#### Scenario: A short CPU period is rejected

- **WHEN** a group sets `cpu_quota_us` with `cpu_period_us = 500`
- **THEN** validation fails with `InvalidArgument`
