# Spec Delta

## ADDED Requirements

### Requirement: Launch-time resource setup

`StartOptions` SHALL carry an optional memory limit and working folder. The
child setup SHALL apply them after `setsid` and before `exec`: `chdir` for the
folder and `RLIMIT_AS` for the memory limit. A failure SHALL use the existing
setup-handshake failure path and SHALL NOT publish `resource.json`.

#### Scenario: Working folder is applied before exec

- **WHEN** a start supplies a working folder
- **THEN** the child changes to it before executing the target

#### Scenario: Memory limit failure fails the start

- **WHEN** `setrlimit(RLIMIT_AS)` fails in the child
- **THEN** `start` reports a failure and writes no `resource.json`

### Requirement: Pre-fork trust gate

When a start supplies an expected executable digest, the EM SHALL verify the
executable in the parent before layout creation and forking. A relative command,
missing file, read failure, or digest mismatch SHALL fail with
`core::ErrorCode::SecurityError` and SHALL create no instance layout.

#### Scenario: Trust gate runs before the fork

- **WHEN** a trusted start is requested
- **THEN** the digest check completes before any child process is created

#### Scenario: Failed trust gate creates no layout

- **WHEN** the digest does not match
- **THEN** no instance directory, `resource.json`, or `exit.json` is published
