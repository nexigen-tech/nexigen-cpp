# Spec Delta

## MODIFIED Requirements

### Requirement: Environment overrides are documented and typed

The system SHALL apply these environment overrides, each parsed with the same validation as the JSON field it replaces: `NEXIGEN_LOG_LEVEL`, `NEXIGEN_LOG_DIR`, `NEXIGEN_LOG_MAX_FILE_SIZE_BYTES`, `NEXIGEN_LOG_CPU_AFFINITY`, `NEXIGEN_LOG_RING_CAPACITY`, `NEXIGEN_LOG_MAX_PRODUCER_THREADS`, `NEXIGEN_LOG_MESSAGE_CAPACITY`, and `NEXIGEN_LOG_OVERFLOW_POLICY`. Unset variables SHALL leave the lower-priority value untouched.

#### Scenario: Level override parses enum names

- **WHEN** `NEXIGEN_LOG_LEVEL=debug` and no higher-priority source applies
- **THEN** the merged `initial_log_level` is `LogLevel::Debug`

#### Scenario: Affinity override accepts a core list

- **WHEN** `NEXIGEN_LOG_CPU_AFFINITY=0,2-3`
- **THEN** the merged `flush_cpu_affinity` contains core ids 0, 2, and 3

#### Scenario: Unset variables do not erase file values

- **WHEN** a file sets `max_file_size_bytes` and `NEXIGEN_LOG_MAX_FILE_SIZE_BYTES` is unset
- **THEN** the merged `max_file_size_bytes` equals the file value

#### Scenario: Message capacity override parses a positive integer

- **WHEN** `NEXIGEN_LOG_MESSAGE_CAPACITY=256` and no higher-priority source applies
- **THEN** the merged `message_capacity` equals `256` and `message_capacity_set` is `true`

### Requirement: Merged settings apply to logger configuration

The system SHALL provide apply helpers that populate a `FileLoggerConfig` and a `ConsoleLogger` configuration from merged settings. The file mapping SHALL transfer the log level, log root directory, maximum file size, and CPU affinity list; the console mapping SHALL transfer the initial level. The application name SHALL be supplied separately through the app layer, not through logging settings. Returned string views SHALL remain valid while the source `logging::Settings` is alive.

#### Scenario: File config receives every mapped field

- **WHEN** merged settings set level `warning`, root `/var/log/hil`, max file size `4096`, and affinity `{1, 3}`
- **THEN** the applied `FileLoggerConfig` carries those values and `core_mask` equals the mask covering cores 1 and 3

#### Scenario: Empty affinity means OS placement

- **WHEN** merged `flush_cpu_affinity` is empty
- **THEN** the applied `FileLoggerConfig::core_mask` is `0`

## REMOVED Requirements

### Requirement: App name is a local-only setting

**Reason**: The application name moves out of logging settings into `app::Settings` (sourced from the CLI), so logging no longer owns an `app_name` setting.
**Migration**: The application name is now supplied to `Infrastructure::initialize` and passed into the logger via the `emplace_logger` hook (see `app/application` and `app/settings`).
