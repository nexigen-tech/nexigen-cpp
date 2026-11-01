# Spec Delta

## ADDED Requirements

### Requirement: Child environment and CPU affinity are configurable

The launch contract SHALL accept optional per-child environment entries and an optional allowed-CPU list. It SHALL apply them in the child after the inherited environment and before `exec`, and SHALL reject an out-of-range CPU index with `InvalidArgument` before `fork`. Identity variables set by the runner SHALL take precedence over caller-supplied environment entries.

#### Scenario: Environment entries are visible to the child

- **WHEN** the caller supplies environment entries at launch
- **THEN** the child observes those names and values in its environment

#### Scenario: Runner identity takes precedence

- **WHEN** a caller-supplied entry uses a `NEXIGEN_*` identity name
- **THEN** the child observes the value assigned by the runner

#### Scenario: CPU affinity restricts the child

- **WHEN** the caller supplies an allowed-CPU list
- **THEN** the child's allowed CPU set equals that list

#### Scenario: Invalid CPU index is rejected before fork

- **WHEN** the allowed-CPU list contains an index outside the online set
- **THEN** the launch returns `InvalidArgument` without creating a child process

## MODIFIED Requirements

### Requirement: Stdout and stderr are captured to files

Before `exec`, the runner SHALL open `stdout.log` and `stderr.log` in the stream directory supplied for the attempt and dup2 them onto the child's file descriptors 1 and 2. The runner MAY capture only stderr when stdout is too large; the default SHALL capture both. A start without an attempt directory SHALL truncate both files in the instance directory; a restart SHALL use a new attempt directory and MUST NOT truncate earlier attempts.

#### Scenario: Child stdout goes to the instance log

- **WHEN** the child writes to stdout
- **THEN** the output appears in the attempt stream directory's `stdout.log`, or in `<instance_dir>/stdout.log` when no attempt directory is supplied

#### Scenario: Child stderr goes to the instance log

- **WHEN** the child writes to stderr
- **THEN** the output appears in the attempt stream directory's `stderr.log`, or in `<instance_dir>/stderr.log` when no attempt directory is supplied

#### Scenario: Log files are truncated on each start

- **WHEN** a fresh instance directory is used for a start without an attempt directory
- **THEN** `stdout.log` and `stderr.log` contain only output from the latest run

#### Scenario: Restarted attempts use a new log file

- **WHEN** an existing attempt is restarted for the same control instance directory
- **THEN** the new attempt writes new `stdout.log` and `stderr.log` files in a new attempt directory and the previous attempt's files remain unchanged
