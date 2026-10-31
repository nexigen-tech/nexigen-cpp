# Spec Delta

## MODIFIED Requirements

### Requirement: Environment overrides are documented and typed

The system SHALL apply these environment overrides, each parsed with the same validation as the JSON field it replaces: `NEXIGEN_LOG_LEVEL`, `NEXIGEN_LOG_DIR`, `NEXIGEN_LOG_APP_NAME`, `NEXIGEN_LOG_MAX_FILE_SIZE_BYTES`, `NEXIGEN_LOG_CPU_AFFINITY`, `NEXIGEN_LOG_RING_CAPACITY`, `NEXIGEN_LOG_MAX_PRODUCER_THREADS`, `NEXIGEN_LOG_MESSAGE_CAPACITY`, and `NEXIGEN_LOG_OVERFLOW_POLICY`. Unset variables SHALL leave the lower-priority value untouched.

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

### Requirement: Explicit compile-time capacity limits are checked

An explicitly configured `spsc_ring_buffer_capacity`, `max_producer_threads`, or `message_capacity` SHALL be checked against the selected logger's compile-time limits. A value above a limit SHALL produce `InvalidArgument` and a diagnostic; it MUST NOT be silently truncated.

#### Scenario: Excess explicit ring capacity is rejected
- **WHEN** a source explicitly sets `spsc_ring_buffer_capacity` above the target logger's compile-time shard capacity
- **THEN** the capacity check returns `ErrorCode::InvalidArgument` and the diagnostic names both values

#### Scenario: Excess explicit message capacity is rejected
- **WHEN** a source explicitly sets `message_capacity` above the target logger's compile-time `MessageCapacity`
- **THEN** the capacity check returns `ErrorCode::InvalidArgument` and the diagnostic names both values

#### Scenario: Fitting explicit capacities pass
- **WHEN** explicit ring capacity, producer-thread count, and message capacity are all less than or equal to the target logger's limits
- **THEN** the capacity check returns OK and the logger can be initialized from the applied config

## ADDED Requirements

### Requirement: Message capacity explicit presence is tracked

The system SHALL record whether `message_capacity` was explicitly provided by any source, using a `message_capacity_set` flag, so that only explicitly provided values participate in the compile-time capacity check.

#### Scenario: Explicit message capacity is flagged
- **WHEN** a source sets `message_capacity`
- **THEN** `message_capacity_set` is `true` in the merged `Logging_settings`

#### Scenario: Default message capacity is not flagged
- **WHEN** no source sets `message_capacity`
- **THEN** `message_capacity_set` is `false` and the default value does not participate in the capacity check