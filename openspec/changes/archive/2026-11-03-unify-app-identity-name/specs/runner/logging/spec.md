# Spec Delta

## MODIFIED Requirements

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

### Requirement: Global logging settings supply operational fields

When `<global_settings_path>/logging.json` is readable, the runner SHALL use its `initial_log_level` as the level fallback, its `flush_cpu_affinity` as the logger-worker affinity fallback, its `max_file_size_bytes` for size segmentation, and its throttle settings. Path, file name, and scheduled rotation SHALL remain runner-specific. An invalid or unreadable file SHALL produce a warning and SHALL NOT fail the command.

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

- **WHEN** the manifest sets `runner_name` and the global file is read
- **THEN** `runner.jsonl` carries the `runner_name` value as its app field, unaffected by any logging setting
