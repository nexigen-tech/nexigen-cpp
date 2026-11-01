# Spec Delta

## MODIFIED Requirements

### Requirement: Instance directory layout is deterministic

The runner SHALL create the control instance directory at `<run_dir>/<app_name>/<instance_id>/` from the telemetry-layout identity contract, without consulting the current directory, hostname, or any implicit source. It SHALL contain `resource.json`, `exit.json`, and `settings/` with four JSON files. Runtime streams SHALL live in the instance directory for a single-instance start, or in the attempt artifact directory supplied by a suite launch.

#### Scenario: Directory layout is created on start

- **WHEN** the runner is invoked with a run directory, app name, and instance id
- **THEN** the instance directory, `resource.json`, `settings/` with the four JSON files, the four JSONL streams, `stdout.log`, and `stderr.log` are created before the child process starts; streams are in the instance directory when no artifact root is supplied and in the attempt artifact directory otherwise, and `exit.json` appears when the child terminates

#### Scenario: Non-null identity fields produce the same layout

- **WHEN** `NEXIGEN_RUN_DIR`, `NEXIGEN_APP_NAME`, and `NEXIGEN_INSTANCE_ID` are set
- **THEN** the same control layout is created without explicit command-line arguments

#### Scenario: Separate artifact root moves runtime streams

- **WHEN** a launch supplies an artifact root
- **THEN** `resource.json`, `exit.json`, and `settings/` remain in the control instance directory while the four JSONL streams, `stdout.log`, and `stderr.log` are created in the artifact directory

### Requirement: Generated settings contain absolute sink paths

The runner SHALL generate four JSON settings files under `<instance_dir>/settings/`. Each `sink.path` SHALL be absolute and point inside the current stream root: the instance directory for a single-instance start, or the attempt artifact directory for a suite launch. `log_root_directory` SHALL equal that stream root. A source path outside the stream root SHALL be replaced by the deterministic `<stream_root>/<stream>.jsonl` default.

#### Scenario: Four settings files are written

- **WHEN** the runner creates the instance directory
- **THEN** `logging.json`, `metrics.json`, `tracing.json`, and `chrono.json` exist under the `settings/` subdirectory and each contains a valid JSON object

#### Scenario: Every sink.path points under the instance directory

- **WHEN** the runner generates `metrics.json`
- **THEN** the `sink.path` value starts with the current stream root; a path derived from the identity contract ends with `metrics.jsonl`, and a path outside the stream root falls back to `<stream_root>/metrics.jsonl`. The same rule applies to the other three streams.

#### Scenario: Logging settings use the correct root

- **WHEN** `logging.json` is generated
- **THEN** its `log_root_directory` equals the current stream root

#### Scenario: Artifact root relocates sink paths

- **WHEN** a launch supplies an attempt artifact directory
- **THEN** each generated `sink.path` starts with that directory and `log_root_directory` equals it
