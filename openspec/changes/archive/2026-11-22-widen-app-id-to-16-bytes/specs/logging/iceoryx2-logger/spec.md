# Spec Delta

## MODIFIED Requirements

### Requirement: Log frame is a fixed zero-copy frame

`nexigen::logging` SHALL publish each log record as a fixed 256-byte, cache-line-aligned,
trivially-copyable `LogFrame` carrying a monotonic `timestamp_ns`, a 16-byte `app_id`, a
`context_id`, a `thread_id`, a `pid`, a `log_level`, a `payload_size`, a `dropped_counter`, a
wire `format_version`, and a fixed 211-byte `payload` buffer. The frame SHALL contain no pointers,
no owning members, and no heap allocation, so it survives a zero-copy shared-memory exchange
unchanged.

#### Scenario: Frame layout is fixed and pointer-free

- **WHEN** `LogFrame` is instantiated
- **THEN** `sizeof(LogFrame)` equals 256, `alignof(LogFrame)` is 64, and
  `std::is_trivially_copyable_v<LogFrame>` holds

#### Scenario: Frame carries the metadata fields

- **WHEN** a worker packs a rendered message into a frame
- **THEN** the frame's `timestamp_ns`, `app_id`, `thread_id`, `pid`, `log_level`, `payload_size`,
  `dropped_counter`, and `format_version` are set to the record's values, `pid` equals the client
  process's OS PID, and the `payload` holds the rendered message bytes

#### Scenario: Sixteen-byte identifier travels whole

- **WHEN** the producing application has the identifier `CN_LOOP_1`
- **THEN** the frame's `app_id` renders as `CN_LOOP_1` and no byte is truncated

#### Scenario: Payload capacity is bounded

- **WHEN** a rendered message exceeds 211 bytes
- **THEN** the payload is clipped to 211 bytes and `payload_size` reports the stored length

#### Scenario: Unknown wire version is rejected

- **WHEN** a consumer receives a frame whose `format_version` is not the supported value
- **THEN** it rejects the frame instead of interpreting its offsets
