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

### Requirement: Stdout and stderr are captured to files

Before `exec`, the runner SHALL open `stdout.log` and `stderr.log` inside the instance directory and dup2 them onto the child's file descriptors 1 and 2. The runner MAY capture only stderr when stdout is too large; the default SHALL capture both.

#### Scenario: Child stdout goes to the instance log

- **WHEN** the child writes to stdout
- **THEN** the output appears in `<instance_dir>/stdout.log`

#### Scenario: Child stderr goes to the instance log

- **WHEN** the child writes to stderr
- **THEN** the output appears in `<instance_dir>/stderr.log`

#### Scenario: Log files are truncated on each start

- **WHEN** the same instance directory is re-used for a second start
- **THEN** `stdout.log` and `stderr.log` contain only output from the latest run (previous content is discarded)

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
