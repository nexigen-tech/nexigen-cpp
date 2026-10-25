# Spec Delta

## ADDED Requirements

### Requirement: Runnable file logger example

The project SHALL provide an `ex_file_logger` executable that initializes a `FileLogger` from a complete `FileLogger_config` (application name, base directory `/tmp/nexigen-examples-logs`, file name, hourly rotation, a non-zero CPU core mask, and an enabled throttle budget), logs through the variadic API, flushes, and exits 0.

#### Scenario: Configured JSONL file is produced

- **WHEN** the example logs accepted records and flushes successfully
- **THEN** a JSON-lines file exists under the configured base directory in the current hourly period directory and contains one complete JSON object per accepted record

#### Scenario: Configuration is inspectable

- **WHEN** the example renders `FileLogger_config::to_string()`
- **THEN** the text names the application, base directory, file name, initial level, throttle policy, rotation schedule, and core mask

#### Scenario: Affinity outcome is reported

- **WHEN** initialization starts the writer with the configured core mask
- **THEN** the example reports the writer's effective mask or start failure code without hiding pinning problems

#### Scenario: UTF-8 messages survive the pipeline

- **WHEN** the example logs `logger.debug("Прямой вызов метода debug. Давление: {:.4f}", 0.0025);` and flushes
- **THEN** the file contains the rendered Cyrillic sentence with `0.0025` intact and valid JSON escaping

#### Scenario: Cold-path read-back verification

- **WHEN** the example has flushed and reopens the produced file
- **THEN** it verifies the expected records and message text are present, reports a mismatch on failure, and exits 0 only when verification succeeds
