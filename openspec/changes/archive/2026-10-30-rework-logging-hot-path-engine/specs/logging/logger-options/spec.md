# Spec Delta

## Purpose

Defines the unified runtime configuration surface (`LoggerOptions`) and wait-free reconfiguration API (`set_log_level`, `set_spam_protection_config`) for the `nexigen::logging` async hot-path engine, replacing compile-time template parameters with a single configuration structure applied at initialization.

## ADDED Requirements

### Requirement: LoggerOptions defines the engine configuration

The system SHALL provide `LoggerOptions` as the single runtime configuration structure for the async logging engine, applied at initialization.

#### Scenario: Default-constructed options carry documented defaults

- **WHEN** a `LoggerOptions` is default-constructed
- **THEN** `log_root_directory` is `"./logs"`, `app_name` is `"nexigen_app"`, `initial_log_level` is `LogLevel::Info`, `max_file_size_bytes` is `104857600` (0 = no limit), `max_producer_threads` is `64`, `overflow_policy` is `OverflowPolicy::DropNewest`, `worker_poll_interval_us` is `1000`, `sync_flush_on_critical` is `true`, and `spam_protection` holds default `SpamProtectionConfig`

#### Scenario: `spsc_ring_buffer_capacity` rounds up to power of two

- **WHEN** `spsc_ring_buffer_capacity` is set to a non-power-of-two value
- **THEN** the engine uses `std::bit_ceil` of that value as the actual shard capacity

#### Scenario: `to_string` renders every option field

- **WHEN** `to_string()` is called on a `LoggerOptions`
- **THEN** the result is non-empty and contains each field name and its value

### Requirement: SpamProtectionConfig defines anti-spam quotas

The system SHALL provide `SpamProtectionConfig` as the nested anti-spam configuration inside `LoggerOptions`.

#### Scenario: Default-constructed spam protection carries documented defaults

- **WHEN** a `SpamProtectionConfig` is default-constructed
- **THEN** `enable_repeat_suppression` is `true`, `repeat_suppression_window_ms` is `1000`, `repeat_suppression_max_count` is `100000`, `max_messages_per_sec_per_shard` is `100000` (0 = no limit), `max_bytes_per_sec_per_shard` is `52428800` (0 = no limit), `max_total_bytes_per_sec` is `209715200` (0 = no limit), `bypass_min_level` is `LogLevel::Critical`

#### Scenario: `to_string` renders every spam-protection field

- **WHEN** `to_string()` is called on a `SpamProtectionConfig`
- **THEN** the result is non-empty and contains each field name and its value

### Requirement: OverflowPolicy enum controls full-ring behavior

The system SHALL provide `OverflowPolicy` with exactly two values: `DropNewest` (incoming record dropped when ring is full) and `OverwriteOldest` (oldest unread record overwritten atomically).

#### Scenario: Overflow policy values are serializable

- **WHEN** `OverflowPolicy::DropNewest` and `OverflowPolicy::OverwriteOldest` are rendered
- **THEN** `to_string` returns distinct names, `from_string` round-trips both names, and `to_json`/`from_json` operate under `JSON_NOEXCEPTION=1`

### Requirement: Runtime reconfiguration is wait-free

The system SHALL provide `set_log_level(LogLevel) noexcept` and `set_spam_protection_config(const SpamProtectionConfig&) noexcept` publishing new thresholds/quotas through relaxed atomics without blocking, allocating, or taking mutexes.

#### Scenario: Runtime reconfiguration never blocks producers

- **WHEN** `set_log_level` or `set_spam_protection_config` is called concurrently with producer logging
- **THEN** the call completes in bounded steps with no mutex, spin, yield, sleep, or CAS loop, and producers observe the new values on subsequent calls without stalling
