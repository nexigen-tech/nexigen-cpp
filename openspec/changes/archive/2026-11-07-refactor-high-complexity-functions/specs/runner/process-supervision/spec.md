# runner/process-supervision — internal structure (delta)

## Purpose

This delta specifies how the `runner/process-supervision` capability is internally reorganized to reduce cyclomatic complexity in the per-instance supervisor and the `Runner` control surface. The instance directory layout, `resource.json`/`exit.json` contracts, signal escalation rules, and exec handshake remain unchanged.

## Requirements

### Requirement: `supervise_child` delegates to wait helpers

The `supervise_child` function SHALL be split into a blocking wait helper, a deadline wait helper, and a signal escalation helper. The blocking helper SHALL reap the child with `waitpid(..., 0)`. The deadline helper SHALL poll with `waitpid(..., WNOHANG)` and, on expiry, send `SIGTERM`, wait a grace period, and send `SIGKILL` if necessary. The signal helper SHALL encapsulate `kill(-child, signal)` and update the result record.

#### Scenario: Child exits before deadline

- **WHEN** the child terminates during polling
- **THEN** the deadline helper records `reaped=true`, `status_valid=true`, and stops polling

#### Scenario: Deadline expires and forces termination

- **WHEN** the child is still alive when the idle timeout expires
- **THEN** `SIGTERM` is sent, a bounded grace wait is performed, and `SIGKILL` is sent only if the child is not reaped

### Requirement: `Runner::start` is split into validation, layout, and launch phases

The `Runner::start` overload with `StartOptions` SHALL decompose into: validation of command and options, resolution/preparation of the instance layout, opening of log files, construction of the argv array, forking the supervisor, and reading the start report. Each phase SHALL return a `core::Status` and the caller SHALL clean up file descriptors on any failure.

#### Scenario: Empty command returns early

- **WHEN** `command` is empty
- **THEN** validation returns `InvalidArgument` before any directory or file is created

#### Scenario: Failed fork closes all opened fds

- **WHEN** `fork()` fails after logs are opened
- **THEN** all pipe and log file descriptors are closed and `SystemError` is returned

### Requirement: `Runner::stop` uses state helpers

`Runner::stop` SHALL use helpers to resolve the stop target from `resource.json`/`exit.json`, send signals, wait for the instance to settle, and finalize `exit.json`. The identity gate (starttime ticks check) SHALL remain before any signal is sent.

#### Scenario: Already-stopped instance is idempotent

- **WHEN** `exit.json` already exists
- **THEN** the resolve helper returns no target and `stop` succeeds without sending signals

#### Scenario: Recycled PID is not signalled

- **WHEN** the recorded PID is alive but its `/proc` starttime ticks differ from `resource.json`
- **THEN** the resolve helper writes a fallback `exit.json` and no signal is sent

### Requirement: Refactored internals preserve all observable behavior

After decomposition, the on-disk contracts, signal sequences, timeout behavior, and attribution logic SHALL be identical. No public header SHALL gain or lose a symbol.

#### Scenario: Existing runner supervision tests pass unchanged

- **WHEN** the existing tests for start, stop, status, timeout, and identity-gate races run
- **THEN** they pass without modification

### Requirement: Complexity of decomposed functions stays within the project target

Every function introduced or modified by this refactor SHALL have cyclomatic complexity ≤ 15 as measured by lizard. The overall average complexity for `src/runner/runner.cpp` SHALL not increase.

#### Scenario: Post-refactor lizard report

- **WHEN** lizard is run over `src/runner/runner.cpp`
- **THEN** no function in the file exceeds complexity 15
