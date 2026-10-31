# Spec Delta

## Purpose

Defines the per-run, per-app, per-instance directory layout a nexigen application instance lives in, the start-time and exit-time metadata files (`resource.json`, `exit.json`), and the mechanism that generates absolute-path settings so the four telemetry and logging streams land at known locations under one instance root.

## ADDED Requirements

### Requirement: Instance directory layout is deterministic

The runner SHALL create the instance directory at `<run_dir>/<app_name>/<instance_id>/` as supplied or derived by the telemetry-layout identity contract. The runner MUST NOT consult the current directory, hostname, or any implicit source. The layout is:

```
<run_dir>/<app_name>/<instance_id>/
├── resource.json
├── exit.json
├── settings/
│   ├── logging.json
│   ├── metrics.json
│   ├── tracing.json
│   └── chrono.json
├── app.jsonl
├── metrics.jsonl
├── traces.jsonl
├── chrono.jsonl
├── stdout.log
└── stderr.log
```

#### Scenario: Directory layout is created on start

- **WHEN** the runner is invoked with a run directory, app name, and instance id
- **THEN** the instance directory, `resource.json`, `settings/` with the four JSON files, the four JSONL streams, `stdout.log`, and `stderr.log` are created before the child process starts; `exit.json` appears when the child terminates

#### Scenario: Non-null identity fields produce the same layout

- **WHEN** `NEXIGEN_RUN_DIR`, `NEXIGEN_APP_NAME`, and `NEXIGEN_INSTANCE_ID` are set
- **THEN** the same directory layout is created without explicit command-line arguments

### Requirement: resource.json captures start-time metadata

On start the runner SHALL write `resource.json` inside the instance directory with the following fields: `run_id`, `app_name`, `instance_id`, `instance_dir`, `pid`, `ppid`, `start_time_rfc3339`, `start_time_mono_ns`, `version` (optional nexigen-version tag).

#### Scenario: resource.json is present after start

- **WHEN** the runner starts a child process
- **THEN** a `resource.json` file exists in the instance directory and contains at minimum the non-null identity fields and the child PID

#### Scenario: resource.json uses the same names as the telemetry-layout contract

- **WHEN** a human or tool reads `resource.json`
- **THEN** the `run_id`, `app_name`, `instance_id`, and `instance_dir` keys match the semantics of the `Run_identity` fields from `app/telemetry-layout`

### Requirement: exit.json captures exit metadata

When the child process exits the runner SHALL write `exit.json` inside the instance directory with these fields: `exit_code` (or `null` if terminated by signal), `signal` (or `null` if normal exit), `uptime_s` (wall-clock seconds between start and exit), `terminated_by` (`"runner"` when the runner stopped the process, `"self"` when the process exited on its own, or `"unknown"`).

#### Scenario: exit.json records a normal exit

- **WHEN** the child process exits with code 0
- **THEN** `exit.json` contains `"exit_code": 0`, `"signal": null`, `"terminated_by": "self"`, and a positive `uptime_s`

#### Scenario: exit.json records a signal kill

- **WHEN** the runner sends SIGKILL and the process dies from it
- **THEN** `exit.json` contains `"exit_code": null`, `"signal": 9`, and `"terminated_by": "runner"`

### Requirement: Generated settings contain absolute sink paths

The runner SHALL generate JSON settings files under `<instance_dir>/settings/`. Each settings file SHALL set its `sink.path` to the corresponding stream's absolute path under the instance directory. The `log_root_directory` in logging settings SHALL point to the instance directory. Paths MAY be derived via `app/telemetry-layout` resolution or written directly by the runner. When a generated file names a path, that path SHALL be absolute, pointing inside the instance directory.

#### Scenario: Four settings files are written

- **WHEN** the runner creates the instance directory
- **THEN** `logging.json`, `metrics.json`, `tracing.json`, and `chrono.json` exist under the `settings/` subdirectory and each contains a valid JSON object

#### Scenario: Every sink.path points under the instance directory

- **WHEN** the runner generates `metrics.json`
- **THEN** the `sink.path` value starts with `<instance_dir>/`; a path derived from the identity contract ends with `metrics.jsonl`, and a path outside the instance directory falls back to `<instance_dir>/metrics.jsonl`. The same rule applies to the other three streams.

#### Scenario: Logging settings use the correct root

- **WHEN** `logging.json` is generated
- **THEN** its `log_root_directory` equals the instance directory path

### Requirement: resource.json and exit.json are serializable

Both resource and exit metadata SHALL use the project-wide serialisation trio: `to_string` member and free function, non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter`.

#### Scenario: Start metadata round-trips through JSON

- **WHEN** a populated start-time metadata struct passes through `to_json` then `from_json`
- **THEN** every field equals its original value

#### Scenario: Hostile JSON leaves metadata unchanged

- **WHEN** `from_json` receives an array, wrong field types, or extra fields
- **THEN** the target keeps its previous values and no exception escapes

### Requirement: Lifecycle is cold path only

Instance directory creation, metadata writing, settings generation, and all serialisation SHALL execute before the child process starts. They SHALL NOT be called from a tick, producer, or sink callback.

#### Scenario: Creating the layout before launch leaves no residual

- **WHEN** an instance directory is created, populated, and then the process tree is torn down
- **THEN** every file under the instance directory is accounted for and no temporary intermediate state remains