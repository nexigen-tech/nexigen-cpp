# Spec Delta

## MODIFIED Requirements

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

Under the default `BestEffort` mode, an out-of-range CPU index, duplicate entry, unsupported logger index, scheduler rejection, or kernel affinity rejection SHALL produce a warning/startup report and SHALL NOT stop supervision or change application affinity. A failure explicitly configured as `Required` SHALL instead abort runner initialization before applications launch; runner MUST NOT silently continue with an unapplied required RT configuration.

#### Scenario: Invalid control CPU is ignored

- **WHEN** best-effort control placement contains an index outside the online set
- **THEN** a warning is recorded, the control thread is not pinned, and the command proceeds

#### Scenario: Invalid logger CPU is ignored

- **WHEN** best-effort logger placement contains an index the logger cannot use
- **THEN** a warning/startup report is recorded and the logger runs without pinning

#### Scenario: Kernel rejection is ignored

- **WHEN** the kernel rejects requested best-effort affinity or scheduling
- **THEN** a warning/startup report identifies the failed attribute and the process continues with any applied attributes

#### Scenario: Required control or logger failure aborts initialization

- **WHEN** a required runner attribute cannot be applied during suite start
- **THEN** initialization returns `SystemError` before applications launch and no non-RT fallback is presented as success

#### Scenario: Applications are not affected by a failed pin

- **WHEN** runner pinning fails
- **THEN** applications still receive their configured or full CPU set
