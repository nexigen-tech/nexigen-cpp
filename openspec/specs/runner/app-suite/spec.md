# runner/app-suite Specification

## Purpose

Defines a declarative JSON manifest and a detached suite supervisor that launch, monitor, restart, stop, and report a coordinated set of nexigen applications as one operable suite, including settings resolution, per-attempt artifact directories, per-application impact, and restart policy.

## Requirements

### Requirement: Suite manifest has a fixed schema

The manifest SHALL be a JSON object with `version: 1`, `suite_id`, `run_id`, absolute `run_dir` and `artifacts_path`, optional absolute `global_settings_path`, optional `runner_name`, `log_level`, `suite_restart_attempts`, `suite_restart_delay_s`, and a non-empty `apps` array. Each entry SHALL define its identity, command and arguments, impact and restart policy, timeouts, and optional environment, affinity, settings paths, and log level.

#### Scenario: Complete manifest is accepted

- **WHEN** every required suite and application field is supplied with a valid value
- **THEN** the manifest is accepted and each application carries exactly the supplied command, arguments, environment, CPU list, timeouts, restart counts, settings paths, log level, and impact level

#### Scenario: Optional fields default deterministically

- **WHEN** an application omits `env`, `cpus`, `global_settings_path`, `local_settings_path`, and `log_level`
- **THEN** it launches with no additional environment entries, no CPU restriction, the suite global settings path, no local settings source, and the suite log level

#### Scenario: Runner name is optional

- **WHEN** the manifest omits `runner_name`
- **THEN** the runner uses its default runner name for its own telemetry

#### Scenario: Unknown fields are ignored

- **WHEN** the manifest contains fields the runner does not recognize
- **THEN** parsing succeeds and known fields keep their supplied values

### Requirement: Suite manifest is validated before launch

Manifest validation SHALL complete before any process starts. A malformed document, wrong `version`, missing required field, empty `apps`, duplicate `(app, instance)`, unsafe `suite_id`/`app`/`instance`, relative `run_dir` or `artifacts_path`, non-absolute `global_settings_path`, absolute or `..`-containing `local_settings_path`, unknown impact value, unknown log level, negative count or delay, or invalid application CPU set MUST return `InvalidArgument` and start nothing. Application `cpus` SHALL parse into the shared `CpuSet` representation; duplicate, offline, negative, and unrepresentable IDs SHALL be rejected before launch.

#### Scenario: Duplicate application identity is rejected

- **WHEN** two entries share the same `app` and `instance`
- **THEN** validation fails with `InvalidArgument` and no process is created

#### Scenario: Unknown impact value is rejected

- **WHEN** an entry sets `impact_on_app_suite_operability` to a value other than `failure`, `degrade`, or `not_impact`
- **THEN** validation fails with `InvalidArgument`

#### Scenario: Unsafe identity or relative run directory is rejected

- **WHEN** `suite_id`, `app`, or `instance` contains path-unsafe characters, or `run_dir` or `artifacts_path` is not absolute
- **THEN** validation fails with `InvalidArgument`

#### Scenario: Invalid settings paths are rejected

- **WHEN** a suite or application `global_settings_path` is not absolute, or a `local_settings_path` is absolute or contains a `..` segment
- **THEN** validation fails with `InvalidArgument`

#### Scenario: Unknown log level is rejected

- **WHEN** the suite or an application sets `log_level` to a value other than the canonical lowercase logging levels
- **THEN** validation fails with `InvalidArgument`

#### Scenario: CPU index outside the online set is rejected

- **WHEN** `cpus` contains a negative index, a duplicate, or an index greater than or equal to the online CPU count
- **THEN** validation fails with `InvalidArgument`

#### Scenario: CPU above 63 is not rejected merely for its width

- **WHEN** `cpus` contains an online, permitted CPU above 63
- **THEN** validation accepts it and preserves it in the application's `CpuSet`

#### Scenario: Validation failure starts nothing

- **WHEN** any validation rule fails
- **THEN** no instance directory, artifact directory, supervisor, or child process is created

### Requirement: Suite metadata lives in a deterministic suite directory

Metadata SHALL live in `<run_dir>/.suite/<suite_id>/`, separate from control files under `<run_dir>/<app>/<instance>/` and artifacts under `artifacts_path`. `suite.json` SHALL record suite identity, settings and artifact paths, supervisor pid, start time, manifest path, and instance dirs. `suite-state.json` SHALL record aggregate state, restart usage, and per-app state, attempt, and artifact directory. `suite-exit.json` SHALL record the final outcome. All three SHALL be written atomically.

#### Scenario: Suite directory is created on start

- **WHEN** a valid manifest is started
- **THEN** `<run_dir>/.suite/<suite_id>/suite.json` exists and names every application's instance directory and the artifact root, while `suite-state.json` names each current attempt directory

#### Scenario: Another process can operate from the suite directory

- **WHEN** `status` or `stop` runs in a different process with only the suite directory
- **THEN** it reports or stops the same suite without in-memory state from the starting process

#### Scenario: Metadata writes are atomic

- **WHEN** a metadata file is rewritten while another process reads it
- **THEN** the reader sees either the previous complete document or the new complete document, never partial JSON

### Requirement: Suite launch is detached

`start --config` SHALL fork a detached suite supervisor and return success once that supervisor owns the suite and `suite.json` exists. The supervisor SHALL survive caller exit and SHALL launch one per-instance supervisor per application through the existing single-instance contract. Per-application launch failures SHALL be reflected in suite state, not in the `start` exit status; only manifest or supervisor-start failures SHALL make `start` fail.

#### Scenario: Start returns while applications keep running

- **WHEN** `start --config` returns successfully
- **THEN** the caller may exit and every launched application keeps running under the suite supervisor

#### Scenario: A per-application launch failure does not fail start

- **WHEN** one application cannot be launched but the suite supervisor starts
- **THEN** `start` succeeds and `status` reports that application as failed or restarting according to its impact level

#### Scenario: Manifest or supervisor failure fails start

- **WHEN** the manifest is invalid or the suite supervisor cannot be created
- **THEN** `start` returns a non-zero status and no suite is recorded

### Requirement: Restarts follow abnormal termination only

Application restarts SHALL be triggered only by abnormal termination: a non-zero exit code or death by signal. Exit code 0 and an operator-issued `stop` SHALL never restart. For `degrade` and `not_impact` applications the supervisor SHALL retry that application up to `restart_attempts` times, waiting `restart_delay_s` before each retry, reusing the control instance directory while creating a new attempt artifact directory and leaving earlier attempts intact.

#### Scenario: Abnormal exit triggers a delayed retry

- **WHEN** a `degrade` or `not_impact` application exits with a non-zero code or a signal and retries remain
- **THEN** the supervisor waits `restart_delay_s` and launches the application again in the same control instance directory with a new attempt artifact directory

#### Scenario: Exhausted per-application retries mark the application failed

- **WHEN** the application has consumed all `restart_attempts`
- **THEN** it is reported `failed` and is not launched again until the suite is restarted

#### Scenario: Normal exit does not restart

- **WHEN** an application exits with code 0 on its own
- **THEN** it is reported `exited` and is not restarted

#### Scenario: Operator stop does not restart

- **WHEN** an operator stops an application or the whole suite
- **THEN** no restart occurs for any stopped application

#### Scenario: Retries create new artifact files

- **WHEN** an application is restarted after a failure
- **THEN** the new attempt writes a new `stdout.log`, `stderr.log`, `app.jsonl`, `metrics.jsonl`, `traces.jsonl`, and `chrono.jsonl` in a new attempt directory, rewrites `resource.json`, and leaves the previous attempt's files unchanged

### Requirement: Failure impact restarts the whole suite

An abnormal termination or launch failure of a `failure` application SHALL stop every running application, wait `suite_restart_delay_s`, and relaunch the whole suite with a new attempt directory for every application. Each relaunch SHALL consume one shared `suite_restart_attempts`; the failed application's own restart fields SHALL be ignored. When the budget is exhausted the supervisor SHALL stop the suite and record `failed`.

#### Scenario: Failure application crash restarts every application

- **WHEN** a `failure` application terminates abnormally
- **THEN** all other applications are stopped and every application is launched again after `suite_restart_delay_s`

#### Scenario: Suite restart creates new attempt files for every application

- **WHEN** the whole suite is relaunched after a `failure` crash
- **THEN** every application writes its logs and JSONL streams into a new attempt directory and earlier attempts remain intact

#### Scenario: Suite restart budget is shared

- **WHEN** several `failure` applications fail across the suite lifetime
- **THEN** every whole-suite relaunch consumes the same `suite_restart_attempts` budget

#### Scenario: Exhausted suite budget fails the suite

- **WHEN** the last `suite_restart_attempts` value has been consumed and a `failure` application fails again
- **THEN** every application is stopped, the aggregate state is `failed`, and `suite-exit.json` records the failure

#### Scenario: Failure application ignores its own retry budget

- **WHEN** a `failure` application has `restart_attempts` greater than zero
- **THEN** that value does not cause per-application retries; the suite-wide restart path is used instead

### Requirement: Degrade and not-impact levels bound the damage

When a `degrade` application exhausts its own restart attempts it SHALL be reported `failed` while the rest of the suite keeps running, and the aggregate state SHALL become `degraded`. When a `not_impact` application is restarted or exhausts its own attempts it SHALL be reported `restarting` or `failed`, and the aggregate state SHALL not change because of that application. Neither level SHALL trigger a whole-suite restart.

#### Scenario: Degrade exhaustion degrades the suite

- **WHEN** a `degrade` application fails permanently
- **THEN** the other applications keep running and the aggregate state is `degraded`

#### Scenario: Not-impact exhaustion leaves the suite running

- **WHEN** a `not_impact` application fails permanently and no other application has degraded the suite
- **THEN** the other applications keep running and the aggregate state is `running`

#### Scenario: Not-impact retries never change suite state

- **WHEN** a `not_impact` application fails, is retried `restart_attempts` times, and finally stays down
- **THEN** the aggregate state is the same before and after that sequence

#### Scenario: Neither level restarts the suite

- **WHEN** a `degrade` or `not_impact` application fails
- **THEN** no whole-suite restart is triggered

### Requirement: Suite status reports aggregate and application state

Suite `status` SHALL report aggregate `running`, `degraded`, `failed`, `stopped`, or `unknown`, plus per application its state, attempt number, and artifact directory. Per-app state SHALL be `running`, `restarting`, `exited`, `stopped`, `failed`, or `unknown`. State SHALL derive from suite metadata, instance files, and process liveness, and SHALL be identical whether queried through `--config` or the suite directory. `--json` SHALL emit the same values as text.

#### Scenario: Running suite reports every application

- **WHEN** `status` is queried after a successful start
- **THEN** the aggregate state is `running` and every application is reported with its own state, attempt number, and artifact directory

#### Scenario: Degraded and failed suites are distinguishable

- **WHEN** a degrade-level application is permanently failed or the suite restart budget is exhausted
- **THEN** `status` reports `degraded` or `failed` respectively

#### Scenario: Stopped suite reports stopped

- **WHEN** `status` is queried after a successful suite stop
- **THEN** the aggregate state is `stopped`

#### Scenario: Unknown suite has no record

- **WHEN** the suite directory contains no readable `suite.json`
- **THEN** the aggregate state is `unknown`

#### Scenario: JSON output matches text output

- **WHEN** `status --json` is requested
- **THEN** it reports the same aggregate state and the same per-application state, attempt, and artifact directory as the text form

#### Scenario: Status is independent of the query route

- **WHEN** the same suite is queried through the manifest or through its suite directory
- **THEN** both queries report the same states

### Requirement: Suite stop is final and idempotent

Suite `stop` SHALL request a final stop, send SIGTERM to every application group, wait its `stop_grace_s`, send SIGKILL when needed, suppress all restarts, and record aggregate `stopped` plus `suite-exit.json`. It SHALL be idempotent and SHALL finish even when the suite supervisor is unresponsive.

#### Scenario: Stop terminates every application

- **WHEN** `stop` is issued for a running suite
- **THEN** every application group is terminated, no restart occurs, and the aggregate state becomes `stopped`

#### Scenario: Stop is idempotent

- **WHEN** `stop` is issued for a suite that already recorded `stopped`
- **THEN** it returns success without signaling live processes and without changing the recorded outcome

#### Scenario: Stop succeeds when the suite supervisor is gone

- **WHEN** the suite supervisor process no longer exists but applications are still running
- **THEN** `stop` terminates each application from its recorded instance directory and records the final state

### Requirement: Manifest launch settings reach each child

Each manifest application SHALL be launched with exactly its `args`, its `env` additions, an idle timeout of `idle_timeout_s`, and CPU affinity restricted to its `CpuSet` when non-empty. Identity variables `NEXIGEN_*` SHALL take precedence over manifest `env`. An affinity request the kernel rejects SHALL be reported as that application's launch failure. Runner SHALL apply the supplied set without calculating scheduling policy or priority.

#### Scenario: Arguments are passed exactly

- **WHEN** an entry supplies arguments containing spaces, quotes, and option-like text
- **THEN** the child receives the same argument vector without shell interpretation

#### Scenario: Environment additions are visible to the child

- **WHEN** an entry supplies `env` entries
- **THEN** the child observes them in its environment

#### Scenario: Identity variables take precedence

- **WHEN** `env` also sets a `NEXIGEN_*` identity variable
- **THEN** the child observes the runner-assigned identity value

#### Scenario: CPU affinity is restricted

- **WHEN** an entry supplies a non-empty CPU set
- **THEN** the child's allowed CPU set equals that set

#### Scenario: Rejected affinity is a launch failure

- **WHEN** the kernel rejects the requested affinity at launch
- **THEN** that application is reported as failed to launch and its impact policy applies

#### Scenario: Idle timeout terminates the application

- **WHEN** an entry sets a non-zero `idle_timeout_s` and the application outlives it
- **THEN** the application is terminated through the normal stop sequence and its impact policy applies

### Requirement: Settings sources merge per application

Each application SHALL load settings from defaults, then the suite `global_settings_path`, then its own `global_settings_path`, which replaces the suite value, then its `local_settings_path`, resolved relative to the manifest directory. The four fixed files are `logging.json`, `metrics.json`, `tracing.json`, and `chrono.json`. Absent files SHALL keep defaults; an unreadable supplied file SHALL fail the launch with `SystemError`, and invalid values SHALL fail it with `InvalidArgument`.

#### Scenario: Suite global settings apply by default

- **WHEN** an application has no settings override and the suite names a `global_settings_path`
- **THEN** its merged settings carry the values from that directory's four files

#### Scenario: Application global path replaces the suite path

- **WHEN** an application sets its own `global_settings_path`
- **THEN** its settings load from that directory instead of the suite directory

#### Scenario: Local settings override global settings

- **WHEN** both a global source and a `local_settings_path` set the same field
- **THEN** the local value wins in the merged settings

#### Scenario: Local path is resolved from the manifest directory

- **WHEN** `local_settings_path` is `config/plant` and the manifest lives in `/etc/nexigen`
- **THEN** the loader reads the four files from `/etc/nexigen/config/plant/`

#### Scenario: Missing settings files keep defaults

- **WHEN** a supplied settings directory does not contain one of the four files
- **THEN** that subsystem keeps its built-in defaults and the launch continues

#### Scenario: Unreadable settings file fails the launch

- **WHEN** a supplied settings file exists but cannot be read
- **THEN** that application's launch fails with `SystemError` and its impact policy applies

#### Scenario: Invalid settings values fail the launch

- **WHEN** a supplied settings file contains a wrong-typed or out-of-range value
- **THEN** that application's launch fails with `InvalidArgument` and its impact policy applies

### Requirement: Effective log level applies to each application

When the suite or an application names `log_level`, the effective value SHALL be the application value, otherwise the suite value, and the runner SHALL write it into generated `logging.json` and export it as `NEXIGEN_LOG_LEVEL`, overriding settings files and manifest `env`. When neither names a level, the merged settings value SHALL remain and no `NEXIGEN_LOG_LEVEL` override SHALL be added.

#### Scenario: Suite level applies by default

- **WHEN** an application omits `log_level` and the suite sets `log_level` to `debug`
- **THEN** that application starts at `debug`

#### Scenario: Application level overrides the suite

- **WHEN** the suite sets `log_level` to `info` and the application sets `log_level` to `warning`
- **THEN** that application starts at `warning`

#### Scenario: Effective level beats settings and environment

- **WHEN** a settings file or manifest `env` selects another level
- **THEN** the manifest `log_level` value is the level the child observes through `NEXIGEN_LOG_LEVEL`

#### Scenario: Generated logging settings carry the level

- **WHEN** the runner writes `logging.json` for an attempt
- **THEN** `initial_log_level` equals the effective level

#### Scenario: Absent level preserves settings

- **WHEN** neither the suite nor the application names `log_level`
- **THEN** the merged settings level is kept and no `NEXIGEN_LOG_LEVEL` override is added

### Requirement: Generated settings point at the attempt artifacts

The runner SHALL write `logging.json`, `metrics.json`, `tracing.json`, and `chrono.json` into `<instance_dir>/settings/`. Every `sink.path` SHALL be absolute and point inside the current attempt artifact directory; `log_root_directory` SHALL equal that directory. A settings source's sink path SHALL NOT redirect an artifact outside the attempt directory.

#### Scenario: Four settings files are written

- **WHEN** an application is launched
- **THEN** the instance `settings/` directory contains all four valid JSON files

#### Scenario: Sink paths point into the attempt directory

- **WHEN** the runner generates the four files for attempt `0002`
- **THEN** each `sink.path` starts with that attempt's artifact directory and ends with the corresponding stream name

#### Scenario: External sink paths are overridden

- **WHEN** a global or local settings file names a sink outside the attempt directory
- **THEN** the generated file uses the attempt artifact path instead

### Requirement: Each attempt writes to a new artifact directory

Every launch attempt SHALL write its runtime artifacts to `<artifacts_path>/<suite_id>/<app>/<instance>/attempt-<NNNN>/`, numbered from `0001`. That directory SHALL contain `stdout.log`, `stderr.log`, `app.jsonl`, `metrics.jsonl`, `traces.jsonl`, and `chrono.jsonl`. A restart SHALL increment the attempt number and create a new directory; it MUST NOT truncate or overwrite earlier attempts.

#### Scenario: First attempt uses attempt-0001

- **WHEN** an application starts for the first time
- **THEN** its logs, metrics, traces, and timestamps are written under the `attempt-0001` directory

#### Scenario: Restart creates the next attempt directory

- **WHEN** an application is restarted
- **THEN** the new process writes to `attempt-0002` with six new files and the previous files remain unchanged

#### Scenario: Control files stay in the instance directory

- **WHEN** artifacts are written under `artifacts_path`
- **THEN** `resource.json`, `exit.json`, and `settings/` remain in `<run_dir>/<app>/<instance>/`

### Requirement: Suite supervision is cold path only

Suite supervision SHALL run in a separate process and SHALL use only control-path I/O: process creation, polling, signals, and JSON files. It SHALL NOT be called from an application tick, producer, snapshot, or sink callback, and SHALL NOT add work to any real-time hot path.

#### Scenario: Suite supervisor is a separate process

- **WHEN** a suite is running
- **THEN** the suite supervisor pid differs from every application pid and from the starting caller

#### Scenario: No supervision work on application hot paths

- **WHEN** a supervised application runs its tick loop
- **THEN** no suite polling, file I/O, fork, exec, or kill attributable to the suite occurs in that loop

### Requirement: Suite types follow the serialization contract

New suite enums and structures SHALL follow the project serialization contract: `to_string` member and free function, `std::formatter`, and non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. Hostile or partial JSON SHALL leave targets unchanged; a valid document SHALL round-trip every field.

#### Scenario: Impact and state enums round-trip through text and JSON

- **WHEN** `Impact_on_app_suite_operability`, `Suite_state`, and `App_state` values pass through `to_string`, `std::formatter`, `to_json`, and `from_json`
- **THEN** the original values are recovered and the text names match the manifest spellings

#### Scenario: Manifest and status structures round-trip

- **WHEN** populated `App_spec`, `Suite_manifest`, `Suite_metadata`, `Suite_app_status`, and `Suite_status` values pass through `to_json` then `from_json`
- **THEN** every field, including settings paths, log level, attempt number, and artifact directory, equals its original value

#### Scenario: Hostile suite JSON is ignored

- **WHEN** `from_json` receives an array, wrong field types, or extra fields
- **THEN** the target keeps its previous values and no exception escapes
