# Spec Delta

## MODIFIED Requirements

### Requirement: Log frame is a fixed zero-copy frame

`nexigen::logging` SHALL publish each log record as a fixed-size, cache-line-aligned, trivially-copyable `LogFrame` carrying a monotonic `timestamp_ns`, an `app_id`, a `context_id`, a `thread_id`, a `pid`, a `log_level`, a `payload_size`, a `dropped_counter`, and a fixed `payload` buffer. The frame SHALL contain no pointers, no owning members, and no heap allocation, so it survives a zero-copy shared-memory exchange unchanged.

#### Scenario: Frame layout is fixed and pointer-free

- **WHEN** `LogFrame` is instantiated
- **THEN** `sizeof(LogFrame)` equals the documented fixed size, `alignof(LogFrame)` is a multiple of the cache line, and `std::is_trivially_copyable_v<LogFrame>` holds

#### Scenario: Frame carries the metadata fields

- **WHEN** a worker packs a rendered message into a frame
- **THEN** the frame's `timestamp_ns`, `app_id`, `thread_id`, `pid`, `log_level`, `payload_size`, and `dropped_counter` are set to the record's values, `pid` equals the client process's OS PID, and the `payload` holds the rendered message bytes