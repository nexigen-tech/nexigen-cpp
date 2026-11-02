# Spec Delta

## MODIFIED Requirements

### Requirement: Settings diagnostics are serializable

`logging::Settings` SHALL provide member and free `to_string()`, a `std::formatter` specialization, and `to_json`, but SHALL NOT provide `from_json`, because it is reconstructed by the loader rather than deserialized. The shared `SettingsDiagnostic` and `SettingsPaths` serialization contract SHALL be provided by `core/settings-vocabulary`.

#### Scenario: Diagnostic renders its source and field

- **WHEN** `to_string()` is called on a diagnostic recorded for the local file's `initial_log_level`
- **THEN** the text names the local source, the field, and the error description

#### Scenario: Diagnostic JSON round-trips

- **WHEN** a `core::SettingsDiagnostic` passes through `to_json` then `from_json`
- **THEN** the recovered source, field, and message equal the originals under `JSON_NOEXCEPTION=1`

#### Scenario: Settings format through std::format

- **WHEN** `std::format("{}", settings)` is evaluated for a loaded `logging::Settings`
- **THEN** the result equals `settings.to_string()`

### Requirement: Merged settings apply to logger configuration

The system SHALL provide apply helpers that populate a `FileLoggerConfig` and a `ConsoleLogger` configuration from merged settings. The file mapping SHALL transfer the log level, app name, log root directory, maximum file size, and CPU affinity list; the console mapping SHALL transfer the initial level. Returned string views SHALL remain valid while the source `logging::Settings` is alive.

#### Scenario: File config receives every mapped field

- **WHEN** merged settings set level `warning`, app `"hil"`, root `/var/log/hil`, max file size `4096`, and affinity `{1, 3}`
- **THEN** the applied `FileLoggerConfig` carries those values and `core_mask` equals the mask covering cores 1 and 3

#### Scenario: Empty affinity means OS placement

- **WHEN** merged `flush_cpu_affinity` is empty
- **THEN** the applied `FileLoggerConfig::core_mask` is `0`

### Requirement: Runnable simple settings example

The project SHALL provide an `ex_logging_settings_simple` executable that demonstrates the shortest loader-to-logger flow: create its own minimal global and local JSON files, load them through explicit `core::SettingsPaths`, initialize a logger from the merged settings, emit a record, flush, and exit 0.

#### Scenario: Example creates its own inputs

- **WHEN** `ex_logging_settings_simple` runs with no arguments and no pre-existing configuration files
- **THEN** it creates a temporary directory with one global JSON and one local JSON file and loads them through explicit paths, without requiring environment variables

#### Scenario: Logger initializes from the merged files

- **WHEN** the example has loaded non-empty global and local settings
- **THEN** `FileLogger` initialization succeeds, at least one record is logged and flushed, and the process exits 0

#### Scenario: Failures are visible

- **WHEN** loading, logger initialization, or flush fails
- **THEN** the example prints the failure (and any loader diagnostics) and exits with a non-zero status
