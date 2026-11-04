# Spec Delta

## Purpose

`worker_poll_interval_us` (default `1000`) becomes a field the async logging engine actually reads: it is the ceiling of the background writer's idle sleep, transferred into the logger configs and enforced by the worker.

## MODIFIED Requirements

### Requirement: LoggerOptions defines the engine configuration

The system SHALL provide `LoggerOptions` as the single runtime configuration structure for the async logging engine, applied at initialization. Its `worker_poll_interval_us` field (default `1000`) SHALL be applied by the loggers as the ceiling of the background writer's idle sleep (see `core/worker-tuning`).

#### Scenario: Default-constructed options carry documented defaults

- **WHEN** a `LoggerOptions` is default-constructed
- **THEN** `log_root_directory` is `"./logs"`, `initial_log_level` is `LogLevel::Info`, `max_file_size_bytes` is `104857600` (0 = no limit), `max_producer_threads` is `64`, `overflow_policy` is `OverflowPolicy::DropNewest`, `worker_poll_interval_us` is `1000`, `sync_flush_on_critical` is `true`, and `spam_protection` holds default `SpamProtectionConfig`

#### Scenario: `spsc_ring_buffer_capacity` rounds up to power of two

- **WHEN** `spsc_ring_buffer_capacity` is set to a non-power-of-two value
- **THEN** the engine uses `std::bit_ceil` of that value as the actual shard capacity

#### Scenario: Poll interval is applied by the loggers

- **WHEN** merged settings set `worker_poll_interval_us` and a logger is initialized from them
- **THEN** the logger's background writer uses that value as its idle-sleep ceiling (clamped to `[50, 100000]`)

#### Scenario: `to_string` renders every option field

- **WHEN** `to_string()` is called on a `LoggerOptions`
- **THEN** the result is non-empty and contains each field name and its value
