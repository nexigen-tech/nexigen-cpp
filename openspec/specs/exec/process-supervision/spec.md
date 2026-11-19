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

Applications SHALL declare execution dependencies that name a process and the required process state, limited to `Running` or `Terminated`. A dependency and its dependent SHALL belong to the same Function Group State; a dependency on a process outside the requested state, a mutual dependency, or a cycle SHALL be rejected at manifest validation. Both the suite supervisor and the `nexigen-em` control path SHALL start a process only after every dependency has reached its required state during the current transition, and both SHALL terminate processes in reverse dependency order. When the dependency is the time-base daemon, the dependent SHALL start only after the daemon is `Running` and the published time base reports `Ready`.

#### Scenario: Dependent waits for its dependency

- **WHEN** application B declares application A as a dependency and A has not reported `Running`
- **THEN** the EM does not start B until A's running notification arrives

#### Scenario: Dependent waits for Terminated

- **WHEN** application D depends on self-terminating C reaching `Terminated`
- **THEN** the EM starts D only after C is reaped

#### Scenario: Termination follows reverse order

- **WHEN** B depends on A reaching `Running` and both are leaving the requested state
- **THEN** the EM terminates B before A

#### Scenario: Cross-state dependency is rejected

- **WHEN** a process in `ADAS:Driving` declares a dependency on a process configured only for `ADAS:Fallback`
- **THEN** manifest validation rejects the configuration

#### Scenario: Mutual dependency is rejected

- **WHEN** A depends on B and B depends on A
- **THEN** manifest validation rejects the cycle

#### Scenario: Missing dependency fails startup

- **WHEN** a dependency never reports `Running` within its startup timeout
- **THEN** the EM fails the dependent's startup with a visible error

#### Scenario: Time-base dependency waits for readiness

- **WHEN** the time-base daemon is `Running` but the published base is not `Ready`
- **THEN** the dependent does not start until the base reports `Ready` or the startup timeout expires

### Requirement: Startup-timeout recovery policy

When a process exceeds the startup timeout of its startup configuration, the EM SHALL log the failure, retry up to the configured restart attempts with the configured delay, and hand the failure to the transition when the budget is exhausted. A termination timeout SHALL force-terminate the process and let the transition continue. This policy applies to both the suite supervisor and the `nexigen-em` control path.

#### Scenario: Timed-out process is logged and recovered

- **WHEN** a process fails to report `Running` within `startup_timeout` and is SIGKILLed
- **THEN** the EM logs the failure and applies the configured restart policy

#### Scenario: Exhausted startup budget fails the transition

- **WHEN** the last configured startup attempt also times out
- **THEN** the process is force-terminated and the transition reports failure

#### Scenario: Termination timeout continues

- **WHEN** a process does not exit within its termination timeout
- **THEN** the EM force-terminates it and continues the transition

### Requirement: Crash cleanup before restart

Before restarting a process that died abnormally, the EM SHALL run the iceoryx2 cleanup of that process's stale node markers and handles, so the relaunched process registers its services with the same identifiers without `AlreadyExists` conflicts or stuck locks.

#### Scenario: Relaunch reuses the same identifiers

- **WHEN** a crashed process is restarted under its restart policy
- **THEN** the EM cleans the stale iceoryx2 markers first and the process re-registers its services successfully

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

### Requirement: Child credentials are dropped before exec

The launch contract SHALL accept an optional `uid` and `gid` per application
entry (`uid` alone implies `gid = uid`). When configured, the child SHALL clear
supplementary groups if it is privileged, call `setgid`, then `setuid`, verify
the drop with `getuid`/`geteuid`/`getgid`, and only then `exec`. Any failed step
SHALL report a setup failure and start no application. An entry declaring `uid`
or `gid` equal to `0` SHALL be rejected during manifest validation.

#### Scenario: The child runs as the configured user

- **WHEN** an application declares `uid`/`gid` equal to the supervisor's current user
- **THEN** the launched process reports that user id

#### Scenario: UID zero is rejected

- **WHEN** a manifest entry declares `uid` or `gid` equal to `0`
- **THEN** manifest validation fails with `InvalidArgument`

#### Scenario: A failed drop starts nothing

- **WHEN** `setgid` or `setuid` fails in the child
- **THEN** the setup handshake reports a failure and the application does not start

#### Scenario: GID without UID is rejected

- **WHEN** an entry declares `gid` but no `uid`
- **THEN** manifest validation fails with `InvalidArgument`

### Requirement: Non-root launch policy

When `trusted_platform.require_non_root` is set, the execution manager SHALL
refuse to launch an application with effective UID `0` unless the entry declares
an explicit `uid`; the refusal SHALL be `SecurityError`.

#### Scenario: Root is refused under the policy

- **WHEN** the supervisor has effective UID `0`, the policy is set, and the entry has no `uid`
- **THEN** the launch fails with `SecurityError`

#### Scenario: An explicit uid satisfies the policy

- **WHEN** the supervisor has effective UID `0` and the entry declares a non-zero `uid`
- **THEN** the launch proceeds and the child drops to that user

#### Scenario: The policy is off by default

- **WHEN** a manifest omits `require_non_root`
- **THEN** a root supervisor launches applications exactly as before

### Requirement: RT priority capability is opt-in

The launch contract SHALL accept `allow_rt_priority` per application (default
`false`). Unless it is set, the child SHALL drop `CAP_SYS_NICE` from the
capability bounding set before `exec`, so no later `exec` can regain it. A
failed drop on a privileged child SHALL report a setup failure and start no
application; a non-root child SHALL skip the call.

#### Scenario: The default drops CAP_SYS_NICE

- **WHEN** an application is launched without `allow_rt_priority` by a privileged supervisor
- **THEN** the child's capability bounding set no longer contains `CAP_SYS_NICE`

#### Scenario: Opt-in keeps the capability

- **WHEN** an application declares `allow_rt_priority: true`
- **THEN** the bounding set is left unchanged

#### Scenario: A failed drop starts nothing

- **WHEN** the privileged child cannot drop the capability
- **THEN** the setup handshake reports a failure and the application does not start

#### Scenario: The opt-in cannot target a non-root uid

- **WHEN** an application declares both `allow_rt_priority: true` and a `uid`
- **THEN** manifest validation fails, because the credential drop would clear
  the capability and the opt-in would be a silent no-op

### Requirement: Recovery restart is admitted asynchronously

A restart requested over the PHM recovery channel SHALL be answered before the restart is performed. The Execution Manager SHALL validate the request, mark the process as terminating, enqueue the restart, and answer `Accepted` — or `Rejected` with a reason — without waiting for the stop or the launch. The restart itself SHALL run later on the control thread through the same stop-then-launch sequence as any other restart.

#### Scenario: Answer arrives before the process stops

- **WHEN** a recovery restart is requested for a running process that needs the full termination grace period
- **THEN** the answer arrives immediately and the stop and relaunch happen afterwards

#### Scenario: Rejection is answered immediately

- **WHEN** a recovery restart names an unknown process or the suite is not operational
- **THEN** the answer is `Rejected` with the corresponding reason and no process is touched

#### Scenario: Accepted restart still publishes lifecycle

- **WHEN** an admitted restart later stops and relaunches the process
- **THEN** `Terminating`, `Terminated`, and `Running` are published for it exactly as for an ordinary restart

### Requirement: Recovery restart consumes the restart budget

A restart requested over the PHM recovery channel SHALL count against the process's configured restart budget instead of bypassing it. When the last permitted attempt also fails to reach `Running`, the Execution Manager SHALL publish `Failed` and stop attempting.

#### Scenario: Each recovery restart spends one attempt

- **WHEN** a process is restarted on PHM request
- **THEN** the configured restart budget decreases by one

#### Scenario: Exhausted budget reports Failed

- **WHEN** the last permitted recovery restart also fails to reach `Running`
- **THEN** the Execution Manager publishes `Failed` for that process and performs no further launch

#### Scenario: Budget exhaustion is not a crash retry

- **WHEN** the restart budget is already spent
- **THEN** a further recovery restart request is answered `Rejected` with `AlreadyRestarting` or a budget reason, and no launch is attempted

### Requirement: Recovery response carries a rejection reason

The recovery answer SHALL carry the request correlation id, the outcome, and a reason. The outcome SHALL be `Accepted`, `InProgress`, or `Rejected`. A rejected answer SHALL name one of `UnknownProcess`, `NotOperational`, `AlreadyRestarting`, `InvalidAction`, or `SystemError`, never `None`.

#### Scenario: Accepted answer echoes the request id

- **WHEN** a restart request carrying a correlation id is admitted
- **THEN** the answer repeats that id with outcome `Accepted` and reason `None`

#### Scenario: Not-operational suite rejects

- **WHEN** a restart is requested while the suite is not in an operational state
- **THEN** the answer is `Rejected` with reason `NotOperational`

#### Scenario: Unsupported action rejects

- **WHEN** a request names an action this channel does not serve
- **THEN** the answer is `Rejected` with reason `InvalidAction` and the watchdog is left to PHM

### Requirement: Time-base loss retry gate

An application that exits with the `time_base_lost` reason SHALL NOT be retried while the time-base daemon is not `Running` with a `Ready` base. The supervisor SHALL hold the retry — without consuming the application's restart budget — and SHALL resume the normal restart policy once the daemon is ready again.

#### Scenario: Retry is held until the daemon returns

- **WHEN** an application exits with `time_base_lost` and the daemon is absent
- **THEN** the supervisor holds the retry, the restart budget is unchanged, and no relaunch is attempted

#### Scenario: Retry resumes after readiness

- **WHEN** the daemon becomes `Running` and the base reports `Ready`
- **THEN** the held retry proceeds under the application's configured restart policy

#### Scenario: Other exit reasons are unaffected

- **WHEN** an application exits for a reason other than `time_base_lost`
- **THEN** the existing restart policy applies without the time-base gate
