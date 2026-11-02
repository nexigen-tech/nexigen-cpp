# runner/process-supervision Specification

## Purpose

Defines how the nexigen-runner launches a child application process under a process group, captures its stdout and stderr, stops it on request or on timeout, collects its exit status, and exposes run/stop/status commands for external coordination.

## Requirements

### Requirement: Child is launched under a new process group

The runner SHALL fork a child process and call `setsid` in the child before `exec`, placing the child and its eventual descendants into a new session and process group. The runner SHALL wait for `SIGCHLD` to detect child termination.

#### Scenario: Child starts in a separate session

- **WHEN** the runner launches a child
- **THEN** `getsid(child_pid)` does not equal the runner's session id

#### Scenario: Killing the runner session does not reach the child

- **WHEN** the runner's session receives a signal
- **THEN** the child's process group is unaffected (the runner forwards stop signals explicitly)

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

### Requirement: Stop delivers SIGTERM then SIGKILL on timeout

The `stop` command SHALL send `SIGTERM` to the child process group. If the process group has not exited after a configurable grace period (default 5 seconds), the runner SHALL send `SIGKILL`. The `stop` command SHALL wait until the process group is fully reaped before returning success.

#### Scenario: SIGTERM stops the process gracefully

- **WHEN** stop is issued and the child handles SIGTERM
- **THEN** the child exits within the grace period and exit.json records the child's exit code with `"terminated_by": "runner"`

#### Scenario: SIGKILL terminates an unresponsive child

- **WHEN** stop is issued and the child ignores SIGTERM for the whole grace period
- **THEN** SIGKILL is sent, the child dies with signal 9, and exit.json records termination signal 9

### Requirement: Status reports lifecycle state

The `status` command SHALL report one of `running`, `stopped`, `exited`, or `unknown` based on the child process group. `running` means the child PID is alive. `stopped` means the runner stopped the process deliberately. `exited` means the process terminated on its own. `unknown` means the runner has no record of this instance.

#### Scenario: Status after start is running

- **WHEN** `status` is queried right after a start
- **THEN** the reported state is `running`

#### Scenario: Status after stop is stopped

- **WHEN** `status` is queried after a successful stop
- **THEN** the reported state is `stopped`

#### Scenario: Status after child self-exits is exited

- **WHEN** `status` is queried after the child exits on its own
- **THEN** the reported state is `exited` and exit.json carries the child's exit code

### Requirement: Idle timeout terminates the child

The runner MAY accept an idle timeout for the process group (in seconds, default 0 = no timeout). When the timeout is non-zero and the child process group is still running after that many seconds, the runner SHALL initiate the same stop sequence as the `stop` command.

#### Scenario: Timeout stops the child

- **WHEN** the runner starts a child with a 2-second idle timeout
- **THEN** after 2 seconds the child is terminated and exit.json records a runner-initiated stop

### Requirement: Process supervision is I/O-only on cold paths

Fork, exec, setsid, file descriptor manipulation, waitid, kill, and timeout polling SHALL execute on the runner's control thread and SHALL NOT be called from a tick, producer, or sink callback. The runner is a separate process from the application it supervises.

#### Scenario: Supervision APIs are not called from application ticks

- **WHEN** a managed application runs its tick loop
- **THEN** the application performs no fork, exec, kill, or waitid calls attributable to the runner
