# Spec Delta

## MODIFIED Requirements

### Requirement: SpanFrame is a fixed pointer-free wire record

`nexigen::tracing::SpanFrame` SHALL be a 384-byte (`alignas(64)`, six cache lines)
trivially-copyable record carrying span identity, kind, status, flags, attribute count, thread id,
start and end timestamps, source-location hashes, line and column, an inline 47-character name, up
to four attributes, a 16-byte application code, process id, sequence, publish timestamp, and
`format_version` 2. It SHALL satisfy `com::concepts::Message`, contain no pointers, and pin its
layout at compile time.

#### Scenario: Layout is pinned

- **WHEN** `SpanFrame` is instantiated
- **THEN** `sizeof(SpanFrame) == 384`, `alignof(SpanFrame) == 64`, and `std::is_trivially_copyable_v<SpanFrame>` holds

#### Scenario: Payload has no address-dependent fields

- **WHEN** a frame is inspected field by field
- **THEN** every source-location field holds a hash value, never a process address, and the span name is stored inline

#### Scenario: Publisher identity travels with the span

- **WHEN** a sink configured with application code `CN_LOOP_1` and process id 42 publishes a frame
- **THEN** the received frame carries `CN_LOOP_1`, 42, the next sequence value, and a non-zero publish timestamp

#### Scenario: Sixteen-byte identifier fits the reserved tail

- **WHEN** the application code uses all 16 permitted bytes
- **THEN** the frame stores all 16 bytes, its size stays 384, and the reserved tail shrinks accordingly

#### Scenario: Long span names are truncated, not overflowed

- **WHEN** a span is created with a name longer than 47 characters
- **THEN** the frame stores the first 47 characters and stays a valid NUL-terminated string

#### Scenario: Zero-copy string attributes are clipped on the wire

- **WHEN** an in-process attribute holds a string longer than the 23-byte inline bound
- **THEN** the published frame carries the first 23 bytes in the inline arm and no pointer

#### Scenario: Version two is required

- **WHEN** a consumer receives a frame whose `format_version` is not 2
- **THEN** it rejects the frame instead of interpreting its offsets

#### Scenario: Frame diagnostics render and serialize

- **WHEN** `to_string()`, `std::format("{}", frame)`, and the ADL JSON round-trip are evaluated
- **THEN** the text names the identifiers, span name, application code, and sequence, the formatter output equals the member output, and every JSON field round-trips

### Requirement: Sink configuration and diagnostics

`Iceoryx2TraceSinkConfig` SHALL carry the service name, a non-empty application code of at most 16
bytes, and the subscriber buffer depth. It SHALL reject an empty or oversized service name and an
empty, oversized, or malformed application code before creating any port. The configuration and the
sink SHALL provide cold-path `to_string`, `std::formatter`, and non-throwing JSON; the sink SHALL
serialize its state and deliberately omit deserialization.

#### Scenario: Invalid configuration is rejected

- **WHEN** the service name is empty, or the application code is empty, malformed, or longer than 16 bytes
- **THEN** validation fails before any publisher or subscriber port is created

#### Scenario: Configuration round-trips through JSON

- **WHEN** a valid configuration is serialized and parsed back
- **THEN** every field equals the original value

#### Scenario: Sink diagnostics round-trip and format

- **WHEN** the sink's `to_string`, `to_json`, and `std::format("{}", sink)` are evaluated
- **THEN** the text and JSON name the service, application code, published, dropped, and hash-cache counters, the formatter matches `to_string`, and no `from_json` exists for the live sink
