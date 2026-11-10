# execution_manager/process-supervision Specification

## Purpose

Defines how the nexigen-execution_manager launches a child application process under a process group, captures its stdout and stderr, stops it on request or on timeout, collects its exit status, and exposes run/stop/status commands for external coordination.

## Requirements

### Requirement: Child is launched under a new process group

The execution_manager SHALL fork a child process and call `setsid` in the child before `exec`, placing the child and its eventual descendants into a new session and process group. The execution_manager SHALL wait for `SIGCHLD` to detect child termination.

#### Scenario: Child starts in a separate session

- **WHEN** the execution_manager launches a child
- **THEN** `getsid(child_pid)` does not equal the execution_manager's session id

#### Scenario: Killing the execution_manager session does not reach the child

- **WHEN** the execution_manager's session receives a signal
- **THEN** the child's process group is unaffected (the execution_manager forwards stop signals explicitly)

### Requirement: Child environment and CPU affinity are configurable

The launch contract SHALL accept optional per-child environment entries, an optional `CpuSet`, and an application name delivered as a `--app-name` argument in the child argv. It SHALL apply the environment and affinity in the child after the inherited environment/affinity reset and before `exec`, and SHALL reject an out-of-range or duplicate CPU index with `InvalidArgument` before `fork`. Identity variables set by the execution_manager SHALL take precedence over caller-supplied environment entries; the application name SHALL be delivered through argv rather than the `NEXIGEN_APP_NAME` environment variable. ExecutionManager SHALL NOT select a scheduling policy or priority for the application.

#### Scenario: Environment entries are visible to the child

- **WHEN** the caller supplies environment entries at launch
- **THEN** the child observes those names and values in its environment

#### Scenario: ExecutionManager identity takes precedence

- **WHEN** a caller-supplied entry uses a `NEXIGEN_*` identity name
- **THEN** the child observes the value assigned by the execution_manager

#### Scenario: Application name is passed via argv

- **WHEN** the execution_manager launches a child with a resolved application name
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
- **THEN** the child receives the execution_manager's normal full online-CPU reset rather than inheriting the execution_manager control pin

### Requirement: Stdout and stderr are captured to files

Before `exec`, the execution_manager SHALL open `stdout.log` and `stderr.log` in the stream directory supplied for the attempt and dup2 them onto the child's file descriptors 1 and 2. The execution_manager MAY capture only stderr when stdout is too large; the default SHALL capture both. A start without an attempt directory SHALL truncate both files in the instance directory; a restart SHALL use a new attempt directory and MUST NOT truncate earlier attempts.

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

The `stop` command SHALL send `SIGTERM` to the child process group. If the process group has not exited after a configurable grace period (default 5 seconds), the execution_manager SHALL send `SIGKILL`. The `stop` command SHALL wait until the process group is fully reaped before returning success.

#### Scenario: SIGTERM stops the process gracefully

- **WHEN** stop is issued and the child handles SIGTERM
- **THEN** the child exits within the grace period and exit.json records the child's exit code with `"terminated_by": "execution_manager"`

#### Scenario: SIGKILL terminates an unresponsive child

- **WHEN** stop is issued and the child ignores SIGTERM for the whole grace period
- **THEN** SIGKILL is sent, the child dies with signal 9, and exit.json records termination signal 9

### Requirement: Status reports lifecycle state

The `status` command SHALL report one of `running`, `stopped`, `exited`, or `unknown` based on the child process group. `running` means the child PID is alive. `stopped` means the execution_manager stopped the process deliberately. `exited` means the process terminated on its own. `unknown` means the execution_manager has no record of this instance.

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

The execution_manager MAY accept an idle timeout for the process group (in seconds, default 0 = no timeout). When the timeout is non-zero and the child process group is still running after that many seconds, the execution_manager SHALL initiate the same stop sequence as the `stop` command.

#### Scenario: Timeout stops the child

- **WHEN** the execution_manager starts a child with a 2-second idle timeout
- **THEN** after 2 seconds the child is terminated and exit.json records a execution_manager-initiated stop

### Requirement: Process supervision is I/O-only on cold paths

Fork, exec, setsid, file descriptor manipulation, waitid, kill, and timeout polling SHALL execute on the execution_manager's control thread and SHALL NOT be called from a tick, producer, or sink callback. The execution_manager is a separate process from the application it supervises.

#### Scenario: Supervision APIs are not called from application ticks

- **WHEN** a managed application runs its tick loop
- **THEN** the application performs no fork, exec, kill, or waitid calls attributable to the execution_manager

### Requirement: Setup handshake completes only after a successful exec

The launch handshake SHALL be exec-correct: the child SHALL write a nonzero
4-byte setup status through the `O_CLOEXEC` pipe only when child setup or
`execvp` fails (with `setsid`, stream redirection, CPU affinity, and
environment application still happening before `exec`), and the pipe closing
through `O_CLOEXEC` after a successful `execvp` — EOF at the supervisor —
SHALL be the success signal the handshake reports.

#### Scenario: Successful exec observes EOF, not a pre-exec magic write

- **WHEN** the child performs its setup steps with no error and `execvp`
  succeeds
- **THEN** the supervisor reads EOF from the handshake pipe (never a
  pre-`execvp` success beacon), `resource.json` is written afterwards, and the
  CLI prints the started confirmation

#### Scenario: Setup failures still fail the start before exec

- **WHEN** any pre-exec child setup step (setsid, dup2, affinity, setenv)
  fails
- **THEN** the child writes its nonzero status through the pipe, the
  supervisor kills the process group, the start reports `SystemError`, and no
  `resource.json` is written

### Requirement: A failed exec fails the start visibly

When the launch handshake reports a failure — including a failed `exec` of a
nonexistent or unexecutable binary — the execution_manager SHALL report `SystemError` for
the start command before any `resource.json` is written; the invoking
`nexigen-exec` CLI SHALL exit nonzero printing a visible `start failed`
diagnostic on stderr; and the execution_manager MUST NOT publish `resource.json` or print
a success line.

#### Scenario: Nonexistent binary fails visibly without resource.json

- **WHEN** the execution_manager starts an application whose `argv[0]` cannot be executed
  (not found or not executable)
- **THEN** the start command exits nonzero printing `start failed` on stderr,
  no `resource.json` appears in the instance directory, and `exit.json` does
  not exist for the failed start

#### Scenario: Redirected streams do not hide the failure

- **WHEN** a failed exec happens after the child's stdout/stderr were
  redirected to the attempt files
- **THEN** the user still observes the failure on the console, because the
  diagnostic is produced by the execution_manager CLI process rather than by the child
  whose streams are already redirected into files

### Requirement: Identity is verified before signals are sent

Before any SIGTERM/SIGKILL goes to a recorded process group, the execution_manager SHALL
verify the target identity: the live process's Linux start-ticks (from
`/proc/<pid>/stat`) SHALL equal the `proc_start_ticks` anchor recorded in
`resource.json` at spawn; only a match SHALL be signalled. `status` SHALL
classify `running` under the same match. An absent or zero anchor keeps the
legacy single-check behavior.

#### Scenario: Recycled PID is not signaled

- **WHEN** the recorded PID was recycled onto an unrelated process so that the
  live process's start ticks differ from `proc_start_ticks` in `resource.json`
- **THEN** `stop` sends no signal to any process group, finalizes the instance
  with `terminated_by: "unknown"`, exits without error, and the unrelated
  process keeps running

#### Scenario: Verified identity is still signalled

- **WHEN** the live supervisor group matches the recorded
  `proc_start_ticks` / PID pair
- **THEN** the stop sequence behaves exactly as before (SIGTERM → grace →
  SIGKILL) and exit.json records the child's termination

#### Scenario: Legacy resource.json without the anchor

- **WHEN** `resource.json` predates the identity anchor (no
  `proc_start_ticks` field)
- **THEN** `stop`/`status` keep the previous single-check behavior instead of
  failing the command

### Requirement: Unreapable child errors do not fabricate success

When any blocking or non-blocking `waitpid` on a supervised child fails with an
error other than EINTR, the execution_manager SHALL treat the child's exit status as
unknown: it SHALL NOT report the child as reaped with a previously stale,
zero, or unrelated status, and any exit record written from that path SHALL use
`terminated_by: "unknown"` with neither `exit_code` nor `signal` asserted. A
genuinely reaped outcome SHALL keep writing the real status exactly as before.

#### Scenario: ECHILD during the SIGKILL leg reports unknown

- **WHEN** the group was reaped elsewhere so the final blocking `waitpid`
  returns -1 with ECHILD
- **THEN** the supervisor does not record `exit_code 0` (or any stale value) as
  a successful reap, and the exit record carries `terminated_by: "unknown"`
  with `exit_code: null` and `signal: null`

#### Scenario: Normal reaping is unchanged

- **WHEN** `waitpid` returns the child with a normal status
- **THEN** exit.json carries the actual exit code or termination signal exactly
  as in prior releases

### Requirement: A failed start reaps the supervisor child

When the launch handshake reports failure (EOF without a report, nonzero
report, or an invalid PID), the launch caller SHALL ensure its supervisor
process is reaped: after the initial non-blocking `waitpid` attempt it SHALL
continue waiting (bounded) until the supervisor is gone, so a failed start
leaves no zombie in the launching process.

#### Scenario: Failed handshake leaves no zombie

- **WHEN** the supervisor reports a start failure or dies before the report
- **THEN** after the launch returns `SystemError` the supervisor child no
  longer exists as a zombie of the launching process

### Requirement: Supervisor environment rejects NUL in values

The launch contract SHALL reject a caller-supplied environment entry whose
value contains an embedded NUL byte with `InvalidArgument` before forking —
on the CLI path exactly as the suite manifest path already does.

#### Scenario: NUL byte in an environment value is rejected before fork

- **WHEN** a caller-supplied environment entry's value contains a NUL byte
- **THEN** the launch returns `InvalidArgument` before any child process is
  created (the same precondition already enforced for keys on the CLI path and
  by suite manifests)

### Requirement: `supervise_child` delegates to wait helpers

The `supervise_child` function SHALL be split into a blocking wait helper, a deadline wait helper, and a signal escalation helper. The blocking helper SHALL reap the child with `waitpid(..., 0)`. The deadline helper SHALL poll with `waitpid(..., WNOHANG)` and, on expiry, send `SIGTERM`, wait a grace period, and send `SIGKILL` if necessary. The signal helper SHALL encapsulate `kill(-child, signal)` and update the result record.

#### Scenario: Child exits before deadline

- **WHEN** the child terminates during polling
- **THEN** the deadline helper records `reaped=true`, `status_valid=true`, and stops polling

#### Scenario: Deadline expires and forces termination

- **WHEN** the child is still alive when the idle timeout expires
- **THEN** `SIGTERM` is sent, a bounded grace wait is performed, and `SIGKILL` is sent only if the child is not reaped

### Requirement: `ExecutionManager::start` is split into validation, layout, and launch phases

The `ExecutionManager::start` overload with `StartOptions` SHALL decompose into: validation of command and options, resolution/preparation of the instance layout, opening of log files, construction of the argv array, forking the supervisor, and reading the start report. Each phase SHALL return a `core::Status` and the caller SHALL clean up file descriptors on any failure.

#### Scenario: Empty command returns early

- **WHEN** `command` is empty
- **THEN** validation returns `InvalidArgument` before any directory or file is created

#### Scenario: Failed fork closes all opened fds

- **WHEN** `fork()` fails after logs are opened
- **THEN** all pipe and log file descriptors are closed and `SystemError` is returned

### Requirement: `ExecutionManager::stop` uses state helpers

`ExecutionManager::stop` SHALL use helpers to resolve the stop target from `resource.json`/`exit.json`, send signals, wait for the instance to settle, and finalize `exit.json`. The identity gate (starttime ticks check) SHALL remain before any signal is sent.

#### Scenario: Already-stopped instance is idempotent

- **WHEN** `exit.json` already exists
- **THEN** the resolve helper returns no target and `stop` succeeds without sending signals

#### Scenario: Recycled PID is not signalled

- **WHEN** the recorded PID is alive but its `/proc` starttime ticks differ from `resource.json`
- **THEN** the resolve helper writes a fallback `exit.json` and no signal is sent

### Requirement: Refactored internals preserve all observable behavior

After decomposition, the on-disk contracts, signal sequences, timeout behavior, and attribution logic SHALL be identical. No public header SHALL gain or lose a symbol.

#### Scenario: Existing execution_manager supervision tests pass unchanged

- **WHEN** the existing tests for start, stop, status, timeout, and identity-gate races run
- **THEN** they pass without modification

### Requirement: Complexity of decomposed functions stays within the project target

Every function introduced or modified by this refactor SHALL have cyclomatic complexity ≤ 15 as measured by lizard. The overall average complexity for `src/exec/execution_manager.cpp` SHALL not increase.

#### Scenario: Post-refactor lizard report

- **WHEN** lizard is run over `src/exec/execution_manager.cpp`
- **THEN** no function in the file exceeds complexity 15

### Requirement: Dependency-ordered startup

Applications SHALL declare a `dependencies` list in the manifest, and the EM SHALL start an application only after every named dependency has reported `Running`; a dependent whose dependency never becomes ready SHALL fail startup with a visible error instead of launching.

#### Scenario: Dependent waits for its dependency

- **WHEN** application B declares application A as a dependency and A has not reported `Running`
- **THEN** the EM does not start B until A's running notification arrives

#### Scenario: Missing dependency fails startup

- **WHEN** a dependency never reports `Running` within its startup timeout
- **THEN** the EM fails the dependent's startup with a visible error

### Requirement: Startup-timeout recovery policy

When a process is forcibly terminated after exceeding its `startup_timeout`, the EM SHALL log the failure and hand the process to its configured recovery rule (restart up to `restart_attempts`), rather than leaving it stopped without a record.

#### Scenario: Timed-out process is logged and recovered

- **WHEN** a process fails to report `Running` within `startup_timeout` and is SIGKILLed
- **THEN** the EM logs the failure and applies the configured restart policy

### Requirement: Crash cleanup before restart

Before restarting a process that died abnormally, the EM SHALL run the iceoryx2 cleanup of that process's stale node markers and handles, so the relaunched process registers its services with the same identifiers without `AlreadyExists` conflicts or stuck locks.

#### Scenario: Relaunch reuses the same identifiers

- **WHEN** a crashed process is restarted under its restart policy
- **THEN** the EM cleans the stale iceoryx2 markers first and the process re-registers its services successfully
