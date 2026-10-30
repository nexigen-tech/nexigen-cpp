# logging/logging-settings Specification

## Purpose

Reads explicitly supplied global and local logging settings, merges them with documented precedence, and turns the merged result into logger construction configuration without touching any hot path.

## Requirements

### Requirement: Settings sources are explicit

The system SHALL read logging settings only from source paths supplied by the caller: an optional global JSON file and an optional local JSON file. It MUST NOT consult implicit paths, environment-dependent directories, or files discovered by scanning the filesystem.

#### Scenario: Caller supplies both paths

- **WHEN** `load_logging_settings` is called with existing global and local paths
- **THEN** both files are parsed in that order and the result reports `global_loaded` and `local_loaded` as true

#### Scenario: Missing source is not a failure

- **WHEN** either supplied path does not exist or an empty path is supplied
- **THEN** the load completes with the corresponding loaded flag false, no diagnostic for that source, and the other source or defaults still apply

#### Scenario: Empty paths yield defaults

- **WHEN** both supplied paths are empty
- **THEN** the result carries default `LoggerOptions` values and reports neither source as loaded

### Requirement: Merge precedence is deterministic

Merged settings SHALL follow the precedence built-in defaults, then global file, then local file, then environment overrides. A field that is absent from a higher-priority source SHALL inherit the value from the lower-priority source, and a field absent from every source SHALL keep the `LoggerOptions` default value.

#### Scenario: Local overrides global

- **WHEN** the global file sets `initial_log_level` to `warning` and the local file sets it to `debug`
- **THEN** the merged `initial_log_level` is `debug`

#### Scenario: Absent local field inherits global

- **WHEN** the global file sets `log_root_directory` and the local file omits it
- **THEN** the merged `log_root_directory` equals the global value

#### Scenario: Absent everywhere keeps default

- **WHEN** neither file nor any environment override sets `max_producer_threads`
- **THEN** the merged value equals the `LoggerOptions` default

#### Scenario: Environment has highest priority

- **WHEN** a file sets `initial_log_level` to `warning` and the corresponding environment override sets `error`
- **THEN** the merged `initial_log_level` is `error`

### Requirement: App name is a local-only setting

`app_name` SHALL be read only from the local source: the local JSON file or its environment override. A global file containing `app_name` MUST have that field ignored.

#### Scenario: Global app name is ignored

- **WHEN** the global file sets `app_name` to `"global-app"` and the local file does not set it
- **THEN** the merged `app_name` equals the `LoggerOptions` default, not `"global-app"`

#### Scenario: Local app name wins

- **WHEN** the local file sets `app_name` to `"hil-runner"`
- **THEN** the merged `app_name` is `"hil-runner"`

### Requirement: Environment overrides are documented and typed

The system SHALL apply these environment overrides, each parsed with the same validation as the JSON field it replaces: `NEXIGEN_LOG_LEVEL`, `NEXIGEN_LOG_DIR`, `NEXIGEN_LOG_APP_NAME`, `NEXIGEN_LOG_MAX_FILE_SIZE_BYTES`, `NEXIGEN_LOG_CPU_AFFINITY`, `NEXIGEN_LOG_RING_CAPACITY`, `NEXIGEN_LOG_MAX_PRODUCER_THREADS`, and `NEXIGEN_LOG_OVERFLOW_POLICY`. Unset variables SHALL leave the lower-priority value untouched.

#### Scenario: Level override parses enum names

- **WHEN** `NEXIGEN_LOG_LEVEL=debug` and no higher-priority source applies
- **THEN** the merged `initial_log_level` is `LogLevel::Debug`

#### Scenario: Affinity override accepts a core list

- **WHEN** `NEXIGEN_LOG_CPU_AFFINITY=0,2-3`
- **THEN** the merged `flush_cpu_affinity` contains core ids 0, 2, and 3

#### Scenario: Unset variables do not erase file values

- **WHEN** a file sets `max_file_size_bytes` and `NEXIGEN_LOG_MAX_FILE_SIZE_BYTES` is unset
- **THEN** the merged `max_file_size_bytes` equals the file value

### Requirement: Invalid input degrades to best effort

The loader SHALL be best effort: a missing file, malformed JSON, a field of the wrong type, or an unparsable value SHALL be recorded as a diagnostic and SHALL leave the last valid or default value for that field. The loader MUST still populate merged settings and MUST NOT abort, throw, or require the caller to stop logger initialization.

#### Scenario: Malformed JSON is reported but does not stop the load

- **WHEN** the local file contains invalid JSON while the global file is valid
- **THEN** the call returns a failure `Status`, the diagnostics name the local source and parse problem, and the merged settings still contain the global values

#### Scenario: Invalid enum value keeps the previous value

- **WHEN** a file sets `initial_log_level` to `"chatty"` after a valid source set `debug`
- **THEN** a diagnostic is recorded and `initial_log_level` remains `debug`

#### Scenario: Missing files produce no diagnostics

- **WHEN** every supplied path is absent
- **THEN** the returned `Status` is OK and the diagnostic list is empty

### Requirement: Settings diagnostics are serializable

`Settings_diagnostic` and `Settings_paths` SHALL provide member and free `to_string()`, non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization. `Logging_settings` SHALL provide `to_string()`, `to_json`, and a `std::formatter` specialization but SHALL NOT provide `from_json`, because it is reconstructed by the loader rather than deserialized.

#### Scenario: Diagnostic renders its source and field

- **WHEN** `to_string()` is called on a diagnostic recorded for the local file's `initial_log_level`
- **THEN** the text names the local source, the field, and the error description

#### Scenario: Diagnostic JSON round-trips

- **WHEN** a `Settings_diagnostic` passes through `to_json` then `from_json`
- **THEN** the recovered source, field, and message equal the originals under `JSON_NOEXCEPTION=1`

#### Scenario: Settings format through std::format

- **WHEN** `std::format("{}", settings)` is evaluated for a loaded `Logging_settings`
- **THEN** the result equals `settings.to_string()`

### Requirement: Merged settings apply to logger configuration

The system SHALL provide apply helpers that populate a `FileLogger_config` and a `ConsoleLogger` configuration from merged settings. The file mapping SHALL transfer the log level, app name, log root directory, maximum file size, and CPU affinity list; the console mapping SHALL transfer the initial level. Returned string views SHALL remain valid while the source `Logging_settings` is alive.

#### Scenario: File config receives every mapped field

- **WHEN** merged settings set level `warning`, app `"hil"`, root `/var/log/hil`, max file size `4096`, and affinity `{1, 3}`
- **THEN** the applied `FileLogger_config` carries those values and `core_mask` equals the mask covering cores 1 and 3

#### Scenario: Empty affinity means OS placement

- **WHEN** merged `flush_cpu_affinity` is empty
- **THEN** the applied `FileLogger_config::core_mask` is `0`

### Requirement: Explicit compile-time capacity limits are checked

An explicitly configured `spsc_ring_buffer_capacity` or `max_producer_threads` SHALL be checked against the selected logger's compile-time limits. A value above a limit SHALL produce `InvalidArgument` and a diagnostic; it MUST NOT be silently truncated.

#### Scenario: Excess explicit ring capacity is rejected

- **WHEN** a source explicitly sets `spsc_ring_buffer_capacity` above the target logger's compile-time shard capacity
- **THEN** the capacity check returns `ErrorCode::InvalidArgument` and the diagnostic names both values

#### Scenario: Fitting explicit capacities pass

- **WHEN** explicit ring capacity and producer-thread count are less than or equal to the target logger's limits
- **THEN** the capacity check returns OK and the logger can be initialized from the applied config

### Requirement: Default capacities are never checked

Values not explicitly provided by any source SHALL NOT participate in the capacity check, so `LoggerOptions` defaults larger than a logger's template limits cannot reject that logger.

#### Scenario: Default capacities do not reject a smaller logger

- **WHEN** no source sets capacity or producer-thread count, and the target logger's template limits are smaller than the `LoggerOptions` defaults
- **THEN** the capacity check returns OK and the logger uses its compile-time limits

### Requirement: Settings loading is cold path only

Loading, validation, diagnostics, and config application SHALL execute before logger initialization and SHALL NOT be invoked from a logging producer path. The loader may allocate (`std::string`, `nlohmann::json`, diagnostic storage); steady-state logging SHALL retain its zero-allocation and wait-free guarantees.

#### Scenario: Producer path is untouched

- **WHEN** a logger is initialized from loaded settings and producers then log in a measured window
- **THEN** the existing zero steady-state allocation and wait-free latency tests still pass without new allocations attributed to settings

### Requirement: Runnable simple settings example

The project SHALL provide an `ex_logging_settings_simple` executable that demonstrates the shortest loader-to-logger flow: create its own minimal global and local JSON files, load them through explicit `Settings_paths`, initialize a logger from the merged settings, emit a record, flush, and exit 0.

#### Scenario: Example creates its own inputs

- **WHEN** `ex_logging_settings_simple` runs with no arguments and no pre-existing configuration files
- **THEN** it creates a temporary directory with one global JSON and one local JSON file and loads them through explicit paths, without requiring environment variables

#### Scenario: Logger initializes from the merged files

- **WHEN** the example has loaded non-empty global and local settings
- **THEN** `FileLogger` initialization succeeds, at least one record is logged and flushed, and the process exits 0

#### Scenario: Failures are visible

- **WHEN** loading, logger initialization, or flush fails
- **THEN** the example prints the failure (and any loader diagnostics) and exits with a non-zero status
