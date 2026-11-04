# runner/process-supervision delta spec

## ADDED Requirements

### Requirement: Identity is verified before signals are sent

Before any SIGTERM/SIGKILL goes to a recorded process group, the runner SHALL
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
error other than EINTR, the runner SHALL treat the child's exit status as
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
