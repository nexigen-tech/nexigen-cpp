# Spec Delta

## MODIFIED Requirements

### Requirement: LoggerOptions defines the engine configuration

The system SHALL provide `LoggerOptions` as the single runtime configuration structure for the async logging engine, applied at initialization.

#### Scenario: Default-constructed options carry documented defaults

- **WHEN** a `LoggerOptions` is default-constructed
- **THEN** `log_root_directory` is `"./logs"`, `initial_log_level` is `LogLevel::Info`, `max_file_size_bytes` is `104857600` (0 = no limit), `max_producer_threads` is `64`, `overflow_policy` is `OverflowPolicy::DropNewest`, `worker_poll_interval_us` is `1000`, `sync_flush_on_critical` is `true`, and `spam_protection` holds default `SpamProtectionConfig`

#### Scenario: `spsc_ring_buffer_capacity` rounds up to power of two

- **WHEN** `spsc_ring_buffer_capacity` is set to a non-power-of-two value
- **THEN** the engine uses `std::bit_ceil` of that value as the actual shard capacity

#### Scenario: `to_string` renders every option field

- **WHEN** `to_string()` is called on a `LoggerOptions`
- **THEN** the result is non-empty and contains each field name and its value
