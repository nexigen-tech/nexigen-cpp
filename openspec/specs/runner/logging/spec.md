# runner/logging Specification

## Purpose

Defines how `nexigen-run` records its own CLI and suite-supervisor diagnostics in a durable JSONL log, including level and path selection, fallback behavior, event coverage, and a fork-safe logger lifecycle.

## Requirements

### Requirement: Runner diagnostics use the project file logger

`nexigen-run` SHALL write its own diagnostics through `nexigen::logging::FileLogger` with the application name taken from the manifest `runner_name` (default `nexigen-run` when absent), file name `runner.jsonl`, and scheduled rotation disabled. The output SHALL be append-only JSON lines, and it SHALL NOT contain application log records. Size-based segmentation MAY occur only when global logging settings supply a non-zero maximum file size.

#### Scenario: Suite run writes runner.jsonl

- **WHEN** a valid suite is started
- **THEN** `<artifacts_path>/<suite_id>/runner.jsonl` exists and contains one JSON object per runner event

#### Scenario: Records are append-only

- **WHEN** the same suite is started or operated again
- **THEN** new records are appended and earlier records remain unchanged

#### Scenario: Application logs stay separate

- **WHEN** an application writes its own `app.jsonl`
- **THEN** runner events are not written into that file and application records are not written into `runner.jsonl`

### Requirement: Log location follows the manifest and suite metadata

The runner SHALL write `runner.jsonl` under `<artifacts_path>/<suite_id>/` when a valid suite manifest or a readable `suite.json` supplies both values. With `--suite-dir`, those values SHALL come from `suite.json`. When the manifest is absent or invalid, or `suite.json` is unreadable, the runner SHALL use `<cwd>/.nexigen-run/runner.jsonl`.

#### Scenario: Valid manifest selects the suite artifact directory

- **WHEN** `start` or `stop` receives a valid manifest
- **THEN** the runner log path is `<artifacts_path>/<suite_id>/runner.jsonl`

#### Scenario: Suite directory selects the recorded artifact path

- **WHEN** `status` or `stop` receives `--suite-dir` with readable `suite.json`
- **THEN** the runner log path is derived from that document's `artifacts_path` and `suite_id`

#### Scenario: Missing manifest uses the working-directory fallback

- **WHEN** a single-instance command is used, or the manifest is absent or invalid
- **THEN** the runner log path is `<cwd>/.nexigen-run/runner.jsonl`

#### Scenario: Unreadable suite metadata uses the fallback

- **WHEN** `--suite-dir` points at a directory without readable `suite.json`
- **THEN** the runner log path is the working-directory fallback

### Requirement: Runner level follows manifest, then global settings

The runner SHALL select its level in this order: suite-level `log_level`; otherwise `initial_log_level` from `<global_settings_path>/logging.json`; otherwise `info`. With `--suite-dir`, the global settings path SHALL come from `suite.json`. An absent or invalid manifest, absent level, or unreadable settings file SHALL fall through to `info`. Per-application `log_level` values SHALL NOT change runner logging.

#### Scenario: Explicit suite level wins

- **WHEN** the manifest sets `log_level` to `debug` and the global file sets `info`
- **THEN** runner records at debug and above are written

#### Scenario: Global level applies when the manifest omits it

- **WHEN** a valid manifest omits `log_level` and its global file sets `warning`
- **THEN** runner logging uses `warning`

#### Scenario: Missing settings fall through to info

- **WHEN** the manifest omits `log_level` and no readable logging settings exist
- **THEN** runner logging uses `info`

#### Scenario: Suite directory uses recorded global settings

- **WHEN** `--suite-dir` points at a suite whose `suite.json` records a global settings path with `initial_log_level` `error`
- **THEN** runner logging uses `error`

#### Scenario: Application level does not affect the runner

- **WHEN** an application entry sets `log_level` to `verbose` while the suite level and global file omit a level
- **THEN** runner logging remains at `info`

### Requirement: Global logging settings supply operational fields

When `<global_settings_path>/logging.json` is readable, the runner SHALL use its `initial_log_level` as the level fallback, its canonical `worker_thread` or legacy `flush_cpu_affinity` as the logger-worker fallback, its `max_file_size_bytes` for size segmentation, and its throttle settings. Path, file name, and scheduled rotation SHALL remain runner-specific. An invalid or unreadable file SHALL produce a warning and SHALL NOT fail the command.

#### Scenario: Operational fields are applied

- **WHEN** the global file sets a debug level, a logger worker thread spec, a maximum size, and a throttle
- **THEN** runner logging uses those values while still writing `runner.jsonl` under the runner path

#### Scenario: Legacy global affinity remains readable

- **WHEN** the global file supplies only `flush_cpu_affinity`
- **THEN** the runner normalizes it into the logger worker CPU set with best-effort application

#### Scenario: Missing global file keeps runner defaults

- **WHEN** no global logging file exists
- **THEN** runner logging uses the manifest level or `info`, no logger pinning or scheduler request, no size limit, and no throttle

#### Scenario: Invalid global file is non-fatal

- **WHEN** the global file contains invalid values
- **THEN** a warning is recorded, valid defaults are used, and the command and suite continue

#### Scenario: Runner identity is not overridden by the file

- **WHEN** the manifest sets `runner_name` and the global file is read
- **THEN** `runner.jsonl` carries the `runner_name` value as its app field, unaffected by any logging setting

### Requirement: Manifest configures runner thread affinity

The manifest MAY carry canonical `runner_control_thread` and `runner_logger_thread` specs and legacy `runner_control_cpus`/`runner_logger_cpus` lists. The runner SHALL apply the control spec on its CLI/suite-supervisor calling thread before the control loop and the logger spec through the normal logger worker startup. Canonical specs SHALL take precedence over legacy runner lists; logger affinity falls back to global logging worker settings when neither is supplied. Empty CPU sets mean no affinity request. Application processes and per-instance supervisors MUST NOT inherit a runner control pin, and a child without explicit `cpus` SHALL retain the existing full-online-CPU reset behavior. Runner SHALL only apply supplied values, never calculate an RT schedule.

#### Scenario: Control thread is pinned

- **WHEN** `runner_control_thread` supplies a name, CPUs, and scheduler attributes
- **THEN** the CLI or suite-supervisor control thread applies them before its control loop

#### Scenario: Logger worker is pinned

- **WHEN** `runner_logger_thread` supplies a name, CPUs above 63, and scheduler attributes
- **THEN** the FileLogger worker applies the complete spec before writing

#### Scenario: Logger falls back to global affinity

- **WHEN** no canonical or legacy runner logger placement is supplied and the global logging file supplies worker affinity
- **THEN** the logger worker uses that global CPU set

#### Scenario: Applications keep their own affinity

- **WHEN** a supervised application has no explicit `cpus` and the runner control thread is pinned
- **THEN** the application receives the normal full online-CPU reset rather than the runner's pin

#### Scenario: Empty lists mean no pinning

- **WHEN** both runner thread specs have empty CPU sets and the global file has no affinity
- **THEN** the control thread and logger worker retain inherited placement

### Requirement: Suite metadata preserves runner affinity

`suite.json` SHALL record the canonical runner control and logger thread specs so `--suite-dir` can apply the same configuration without the manifest. It SHALL continue reading legacy recorded CPU lists as compatibility inputs. The specs SHALL follow the project's `to_string`, formatter, and non-throwing JSON round-trip mandates through their containing metadata.

#### Scenario: Suite directory reproduces affinity

- **WHEN** `--suite-dir` reads a `suite.json` with named runner control/logger specs on CPUs above 63
- **THEN** the CLI applies the recorded control spec and the logger uses the recorded worker spec

#### Scenario: Legacy suite metadata remains readable

- **WHEN** an older `suite.json` contains only `runner_control_cpus` and `runner_logger_cpus`
- **THEN** those lists are normalized into best-effort thread specs

#### Scenario: Missing runner lists mean no pinning

- **WHEN** recorded suite metadata has no runner thread configuration
- **THEN** `--suite-dir` leaves both threads with inherited attributes

### Requirement: Affinity failures are non-fatal warnings

Under the default `BestEffort` mode, an out-of-range CPU index, duplicate entry, unsupported logger index, scheduler rejection, or kernel affinity rejection SHALL produce a warning/startup report and SHALL NOT stop supervision or change application affinity. The warning SHALL report the actual per-attribute application outcome from the `ThreadApplyReport` — which of name, affinity, and scheduler were requested, applied, and failed, plus the first error — and SHALL NOT claim the thread is "unpinned" or name an "invalid cpus" field when a non-CPU attribute failed or when some attributes applied successfully. A failure explicitly configured as `Required` SHALL instead abort runner initialization before applications launch; runner MUST NOT silently continue with an unapplied required RT configuration.

#### Scenario: Invalid control CPU is ignored

- **WHEN** best-effort control placement contains an index outside the online set
- **THEN** a warning is recorded, the control thread is not pinned, and the command proceeds

#### Scenario: Invalid logger CPU is ignored

- **WHEN** best-effort logger placement contains an index the logger cannot use
- **THEN** a warning/startup report is recorded and the logger runs without pinning

#### Scenario: Kernel rejection is ignored

- **WHEN** the kernel rejects requested best-effort affinity or scheduling
- **THEN** a warning/startup report identifies the failed attribute and the process continues with any applied attributes

#### Scenario: Scheduler rejection is reported by attribute

- **WHEN** a best-effort scheduler request is rejected while the CPU set and name are valid
- **THEN** the warning names the scheduler attribute (not "cpus") and the process continues

#### Scenario: Partial application is reported accurately

- **WHEN** name and affinity apply but the scheduler is rejected under `BestEffort`
- **THEN** the warning reports the applied name and affinity and the failed scheduler separately rather than stating the thread is unpinned

#### Scenario: Required control or logger failure aborts initialization

- **WHEN** a required runner attribute cannot be applied during suite start
- **THEN** initialization returns `SystemError` before applications launch and no non-RT fallback is presented as success

#### Scenario: Applications are not affected by a failed pin

- **WHEN** runner pinning fails
- **THEN** applications still receive their configured or full CPU set

### Requirement: CLI commands record their outcome

`start`, `stop`, and `status` SHALL emit at least one record naming the command, its mode or target, and its result. Failures SHALL be recorded at warning or error level. Option-parsing failures that occur before a log location is resolved MAY remain stderr-only.

#### Scenario: Successful suite start is recorded

- **WHEN** `start --config` succeeds
- **THEN** `runner.jsonl` contains a start record naming the suite and target directory

#### Scenario: Successful single-instance start is recorded

- **WHEN** single-instance `start` succeeds
- **THEN** the fallback runner log contains a start record naming the instance directory

#### Scenario: Stop and status are recorded

- **WHEN** `stop` or `status` completes
- **THEN** a record names the command, target, and resulting state or stop outcome

#### Scenario: Command failure is recorded

- **WHEN** `start`, `stop`, or `status` fails
- **THEN** a warning or error record names the command and the failure reason

### Requirement: Suite supervisor records lifecycle and policy events

The detached suite supervisor SHALL record suite start, application launch and exit, retry scheduling, whole-suite restart, final stop or failure, and forced stop. Records SHALL name the application and instance when applicable and include the attempt or resulting state when known.

#### Scenario: Application exit is recorded

- **WHEN** a supervised application exits
- **THEN** a record names the application, instance, attempt, and exit code or signal

#### Scenario: Retry and failure decisions are recorded

- **WHEN** a retry is scheduled or an application is marked failed
- **THEN** a record names the application, attempt, impact level, and resulting state

#### Scenario: Whole-suite restart is recorded

- **WHEN** a `failure` application triggers a whole-suite restart
- **THEN** records show the triggering application, the consumed suite attempt, and the relaunch

#### Scenario: Stop and forced stop are recorded

- **WHEN** a suite is stopped normally or through the forced path
- **THEN** a record names the final state and the stop reason

### Requirement: Logger lifecycle is fork-safe

After logging is initialized, the runner SHALL flush and join the logger worker before every `fork`, then reinitialize logging in the parent. Before `_exit` it SHALL flush accepted records. A logger worker thread MUST NOT be inherited across `fork`.

#### Scenario: Restart does not inherit a logger thread

- **WHEN** the suite supervisor restarts an application or the whole suite while logging is active
- **THEN** the logger worker is stopped before the fork and a new logger is initialized afterwards

#### Scenario: Pre-restart records are durable

- **WHEN** a restart is triggered after runner records were accepted
- **THEN** those records are present in `runner.jsonl` and restart records follow them

#### Scenario: Final records survive process exit

- **WHEN** the supervisor exits through `_exit`
- **THEN** records accepted before that exit are present in `runner.jsonl`

### Requirement: Logging is best-effort

Logging failures MUST NOT change command exit codes, suite policy, application processes, or supervision results. If the primary location cannot be used, the runner SHALL try `<cwd>/.nexigen-run/runner.jsonl`; if that also fails, it SHALL continue with stderr diagnostics and no file.

#### Scenario: Unwritable primary path falls back

- **WHEN** the suite artifact directory cannot be written
- **THEN** the command and suite behavior are unchanged and the fallback log is attempted

#### Scenario: Both locations fail without changing outcome

- **WHEN** neither the primary nor fallback path can be written
- **THEN** the command still returns its normal result and supervision continues

#### Scenario: Runtime write failure does not stop supervision

- **WHEN** the logger reports a write or flush failure while the suite runs
- **THEN** applications and restart policy continue normally

### Requirement: Runner logging is cold path only

Runner logging SHALL execute only on CLI control paths and in the detached suite supervisor. It MUST NOT be called from an application tick, producer, snapshot, or sink callback and MUST NOT add work to application hot paths.

#### Scenario: Application hot paths are untouched

- **WHEN** a supervised application runs its tick loop
- **THEN** no runner log formatting, enqueue, flush, or file I/O occurs in that loop

#### Scenario: Only runner processes write the runner log

- **WHEN** a suite is running
- **THEN** `runner.jsonl` is written only by `nexigen-run` CLI and supervisor processes

### Requirement: State write failures are reported

Every runner JSON state write SHALL report a failure through the runner warning
channel before returning the error. When runner logging is active the warning
SHALL be a warning-level record in `runner.jsonl`; otherwise it SHALL be one
`nexigen-run:` line on standard error.

#### Scenario: Failed suite state write is recorded

- **WHEN** the suite supervisor cannot write `suite-state.json`
- **THEN** `runner.jsonl` contains a warning that names the failed write

#### Scenario: Failed suite exit write is recorded

- **WHEN** the suite supervisor cannot write `suite-exit.json`
- **THEN** `runner.jsonl` contains a warning that names the failed write

#### Scenario: Failed instance exit write is recorded

- **WHEN** a supervisor cannot write `exit.json`
- **THEN** the failure is reported through the runner warning channel

#### Scenario: Successful writes stay quiet

- **WHEN** a state write succeeds
- **THEN** no warning is emitted for that write
