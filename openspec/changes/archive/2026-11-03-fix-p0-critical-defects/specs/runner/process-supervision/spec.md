# runner/process-supervision delta spec

## ADDED Requirements

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
nonexistent or unexecutable binary — the runner SHALL report `SystemError` for
the start command before any `resource.json` is written; the invoking
`nexigen-run` CLI SHALL exit nonzero printing a visible `start failed`
diagnostic on stderr; and the runner MUST NOT publish `resource.json` or print
a success line.

#### Scenario: Nonexistent binary fails visibly without resource.json

- **WHEN** the runner starts an application whose `argv[0]` cannot be executed
  (not found or not executable)
- **THEN** the start command exits nonzero printing `start failed` on stderr,
  no `resource.json` appears in the instance directory, and `exit.json` does
  not exist for the failed start

#### Scenario: Redirected streams do not hide the failure

- **WHEN** a failed exec happens after the child's stdout/stderr were
  redirected to the attempt files
- **THEN** the user still observes the failure on the console, because the
  diagnostic is produced by the runner CLI process rather than by the child
  whose streams are already redirected into files
