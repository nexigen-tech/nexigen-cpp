# Spec Delta

## Purpose

The apply helpers transfer the worker poll interval into both logger configs, so a value read from the file reaches the logger's idle-backoff ceiling.

## MODIFIED Requirements

### Requirement: Merged settings apply to logger configuration

The system SHALL provide apply helpers that populate a `FileLoggerConfig` and a `ConsoleLogger` configuration from merged settings. The file mapping SHALL transfer the log level, log root directory, maximum file size, CPU affinity list, and worker poll interval; the console mapping SHALL transfer the initial level and the worker poll interval. The application name SHALL be supplied separately through the app layer, not through logging settings. Returned string views SHALL remain valid while the source `logging::Settings` is alive.

#### Scenario: File config receives every mapped field

- **WHEN** merged settings set level `warning`, root `/var/log/hil`, max file size `4096`, affinity `{1, 3}`, and worker poll interval `5000`
- **THEN** the applied `FileLoggerConfig` carries those values (with `core_mask` covering cores 1 and 3 and `worker_poll_interval_us == 5000`)

#### Scenario: Console config receives the poll interval

- **WHEN** merged settings set `worker_poll_interval_us`
- **THEN** the applied `ConsoleLogger` config carries that value

#### Scenario: Empty affinity means OS placement

- **WHEN** merged `flush_cpu_affinity` is empty
- **THEN** the applied `FileLoggerConfig::core_mask` is `0`
