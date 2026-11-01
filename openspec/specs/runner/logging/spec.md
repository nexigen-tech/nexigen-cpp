# runner/logging Specification

## Purpose

Defines how `nexigen-run` records its own CLI and suite-supervisor diagnostics in a durable JSONL log, including level and path selection, fallback behavior, event coverage, and a fork-safe logger lifecycle.

## Requirements

### Requirement: Runner diagnostics use the project file logger

`nexigen-run` SHALL write its own diagnostics through `nexigen::logging::FileLogger` with app name `nexigen-run`, file name `runner.jsonl`, and scheduled rotation disabled. The output SHALL be append-only JSON lines, and it SHALL NOT contain application log records. Size-based segmentation MAY occur only when global logging settings supply a non-zero maximum file size.

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

When `<global_settings_path>/logging.json` is readable, the runner SHALL use its `initial_log_level` as the level fallback, its `flush_cpu_affinity` as the logger-worker affinity fallback, its `max_file_size_bytes` for size segmentation, and its throttle settings. Path, file name, app name, and scheduled rotation SHALL remain runner-specific. An invalid or unreadable file SHALL produce a warning and SHALL NOT fail the command.

#### Scenario: Operational fields are applied

- **WHEN** the global file sets a debug level, a logger affinity, a maximum size, and a throttle
- **THEN** runner logging uses those values while still writing `runner.jsonl` under the runner path

#### Scenario: Missing global file keeps runner defaults

- **WHEN** no global logging file exists
- **THEN** runner logging uses the manifest level or `info`, no logger pinning, no size limit, and no throttle

#### Scenario: Invalid global file is non-fatal

- **WHEN** the global file contains invalid values
- **THEN** a warning is recorded, valid defaults are used, and the command and suite continue

#### Scenario: Runner identity is not overridden by the file

- **WHEN** the global file names a different app or log root
- **THEN** `runner.jsonl` remains under the runner path with app name `nexigen-run`

### Requirement: Manifest configures runner thread affinity

The manifest MAY carry `runner_control_cpus` and `runner_logger_cpus` CPU lists. The runner SHALL pin its CLI/suite-supervisor control thread to `runner_control_cpus` and the logger worker to `runner_logger_cpus` or, when absent, to `flush_cpu_affinity` from logging settings; empty lists mean OS placement. Application processes and per-instance supervisors MUST NOT inherit a runner pin, and a child without explicit `cpus` SHALL receive all online CPUs.

#### Scenario: Control thread is pinned

- **WHEN** `runner_control_cpus` is non-empty
- **THEN** the CLI or suite-supervisor control thread's allowed CPU set equals that list

#### Scenario: Logger worker is pinned

- **WHEN** `runner_logger_cpus` is non-empty
- **THEN** the FileLogger worker reports that CPU mask

#### Scenario: Logger falls back to global affinity

- **WHEN** `runner_logger_cpus` is empty and the global logging file sets `flush_cpu_affinity`
- **THEN** the logger worker uses that mask

#### Scenario: Applications keep their own affinity

- **WHEN** a supervised application has no explicit `cpus` and the runner control thread is pinned
- **THEN** the application receives all online CPUs rather than the runner's pin

#### Scenario: Empty lists mean no pinning

- **WHEN** both runner CPU lists are empty and the global file has no affinity
- **THEN** the control thread and logger worker use OS placement

### Requirement: Suite metadata preserves runner affinity

`suite.json` SHALL record `runner_control_cpus` and `runner_logger_cpus` so `--suite-dir` can apply the same affinity without the manifest.

#### Scenario: Suite directory reproduces affinity

- **WHEN** `--suite-dir` reads a `suite.json` with both runner CPU lists
- **THEN** the CLI applies the recorded control pin and the logger uses the recorded worker pin

#### Scenario: Missing runner lists mean no pinning

- **WHEN** the recorded suite metadata has empty runner CPU lists
- **THEN** `--suite-dir` leaves both threads unconstrained

### Requirement: Affinity failures are non-fatal warnings

An out-of-range CPU index, duplicate entry, unsupported logger index, or kernel rejection SHALL produce a warning and SHALL disable that pinning. It MUST NOT fail `start`, `stop`, or `status`, stop supervision, or change application affinity.

#### Scenario: Invalid control CPU is ignored

- **WHEN** `runner_control_cpus` contains an index outside the online set
- **THEN** a warning is recorded, the control thread is not pinned, and the command proceeds

#### Scenario: Invalid logger CPU is ignored

- **WHEN** `runner_logger_cpus` contains an index the logger cannot use
- **THEN** a warning is recorded and the logger runs without pinning

#### Scenario: Kernel rejection is ignored

- **WHEN** the kernel rejects the requested affinity
- **THEN** a warning is recorded and the process continues unconstrained

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
