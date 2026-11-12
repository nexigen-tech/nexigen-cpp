# Spec Delta

## Purpose

Defines the cross-process span transport: a fixed wire record that can safely cross shared memory plus a client sink that publishes spans from the tracing worker into an iceoryx2 service for a central trace daemon.

## ADDED Requirements

### Requirement: SpanFrame is a fixed pointer-free wire record

`nexigen::tracing::SpanFrame` SHALL be a 384-byte (`alignas(64)`, six cache lines) trivially-copyable record carrying span identity, kind, status, flags, attribute count, thread id, start and end timestamps, source-location hashes, line and column, an inline 47-character name, up to four attributes, a 4-byte application code, process id, sequence, publish timestamp, and a format version. It SHALL satisfy `com::concepts::Message`, contain no pointers, and pin its layout at compile time.

#### Scenario: Layout is pinned

- **WHEN** `SpanFrame` is instantiated
- **THEN** `sizeof(SpanFrame) == 384`, `alignof(SpanFrame) == 64`, and `std::is_trivially_copyable_v<SpanFrame>` holds

#### Scenario: Payload has no address-dependent fields

- **WHEN** a frame is inspected field by field
- **THEN** every source-location field holds a hash value, never a process address, and the span name is stored inline

#### Scenario: Publisher identity travels with the span

- **WHEN** a sink configured with application code `LIDR` and process id 42 publishes a frame
- **THEN** the received frame carries `LIDR`, 42, the next sequence value, and a non-zero publish timestamp

#### Scenario: Long span names are truncated, not overflowed

- **WHEN** a span is created with a name longer than 47 characters
- **THEN** the frame stores the first 47 characters and stays a valid NUL-terminated string

#### Scenario: Zero-copy string attributes are clipped on the wire

- **WHEN** an in-process attribute holds a string longer than the 23-byte inline bound
- **THEN** the published frame carries the first 23 bytes in the inline arm and no pointer

#### Scenario: Frame diagnostics render and serialize

- **WHEN** `to_string()`, `std::format("{}", frame)`, and the ADL JSON round-trip are evaluated
- **THEN** the text names the identifiers, span name, application code, and sequence, the formatter output equals the member output, and every JSON field round-trips

### Requirement: Source locations cross processes as hashes

Source file and function references SHALL be carried as stable 64-bit FNV-1a hashes, because the in-process record points into program-static storage that is not valid in another process. The hash function and its seed SHALL be fixed so two independently built processes agree on the value for identical text.

#### Scenario: Independent producers agree on the hash

- **WHEN** two processes hash the same file path and function name with the pinned FNV-1a variant
- **THEN** both obtain the same 64-bit value, independent of build flags and process address space

#### Scenario: Different text produces different hashes

- **WHEN** a frame is produced for two different source functions
- **THEN** the function-hash fields differ and neither field equals the in-process pointer value

### Requirement: Source hashes are memoized on the worker

Repeated source locations SHALL reuse a previously computed hash instead of rehashing the text. The sink SHALL expose hash hit and miss counters in its diagnostics. The cache SHALL be confined to the provider's worker thread and SHALL NOT add cross-thread synchronization.

#### Scenario: Repeated locations are hashed once

- **WHEN** two spans from the same source location pass through the sink
- **THEN** the frame carries the same hash, the hit counter increases, and no second hash computation is needed

#### Scenario: Cache capacity does not affect correctness

- **WHEN** spans from more distinct locations than the cache holds are published in alternation
- **THEN** every frame still carries the correct hash for its own location and misses are counted

#### Scenario: Missing source location is well-defined

- **WHEN** a span record has a null file or function pointer
- **THEN** the corresponding hash field is zero and no cache entry is created

### Requirement: Sink publishes each span zero-copy from the worker

`Iceoryx2TraceSink` SHALL satisfy `concepts::TraceSink`. On every `on_span` it SHALL convert the record to a `SpanFrame`, fill the publisher identity and timestamps, loan a shared-memory slot, write the frame in place, and send — allocating nothing and touching no producer thread. `flush()` SHALL report success because a sent frame is already durable in shared memory.

#### Scenario: Span round-trips end to end

- **WHEN** a sink and a subscriber are connected to the same service and one span is delivered to `on_span`
- **THEN** the subscriber receives one frame with the same identifiers, timestamps, name, attributes, application code, and process id

#### Scenario: Publication is allocation-free after start

- **WHEN** an armed allocation counter observes the sink while spans are delivered after `start()`
- **THEN** zero heap allocations occur

#### Scenario: Flush does not discard accepted spans

- **WHEN** `flush()` is called after accepted spans were sent
- **THEN** it returns success and every sent frame is still receivable

### Requirement: Publication never blocks and drops are counted

When no shared-memory slot is available, the sink SHALL discard the frame, increment a dropped-frame counter, and return a successful status so the tracing worker keeps draining. The subscriber buffer depth SHALL be requested from configuration at service creation. Diagnostics SHALL expose the published and dropped totals.

#### Scenario: Saturated transport drops without blocking

- **WHEN** the subscriber queue is full and the sink is asked to publish another span
- **THEN** the call returns success, the dropped counter increases, and the worker is not blocked

#### Scenario: Dropped frames are visible in diagnostics

- **WHEN** the sink diagnostic text is rendered
- **THEN** it names the service, the application code, the published total, and the dropped total

### Requirement: Sequence numbers make gaps observable

Each published frame SHALL carry the next value of a per-sink sequence counter starting at zero. A consumer SHALL detect a missing frame from a non-consecutive sequence carrying the same application code and process id. The sink SHALL NOT require an event notification per span; consumers drain the queue on their own interval.

#### Scenario: Consecutive publications increment the sequence

- **WHEN** three frames are published successfully in a row
- **THEN** their sequences are `n`, `n+1`, and `n+2`

#### Scenario: A dropped frame leaves a gap

- **WHEN** one publication is dropped for lack of a slot and the next one succeeds
- **THEN** the received sequences skip the dropped value, so the consumer can count the loss

#### Scenario: Idle sink produces no notifications

- **WHEN** no spans are delivered to the sink
- **THEN** the sink emits no per-span event traffic and the consumer can rely on interval draining

### Requirement: Per-span formation stays within its budget

Forming and publishing one frame SHALL stay within a median of 400 nanoseconds per span in the strict Release x86-64 configuration, covering mapping, hashing (including cache misses), timestamping, and publication. The measurement SHALL report the median and tail percentiles; the gate SHALL be compiled out in Debug and sanitizer builds.

#### Scenario: Strict configuration enforces the gate

- **WHEN** a warm Release loop measures `on_span` end to end
- **THEN** the median is at most 400 nanoseconds and the tail percentiles are printed

#### Scenario: Non-strict configuration measures without failing

- **WHEN** the suite runs under Debug, ThreadSanitizer, or AddressSanitizer
- **THEN** the same measurement runs and prints its numbers without asserting the timing gate, while still asserting allocation freedom

### Requirement: Sink configuration and diagnostics

`Iceoryx2TraceSinkConfig` SHALL carry the service name, a non-empty application code of at most four bytes, and the subscriber buffer depth. It SHALL reject an empty or oversized service name and an empty or oversized application code before creating any port. The configuration and the sink SHALL provide cold-path `to_string`, `std::formatter`, and non-throwing JSON; the sink SHALL serialize its state and deliberately omit deserialization.

#### Scenario: Invalid configuration is rejected

- **WHEN** the service name is empty, or the application code is empty or longer than four bytes
- **THEN** validation fails before any publisher or subscriber port is created

#### Scenario: Configuration round-trips through JSON

- **WHEN** a valid configuration is serialized and parsed back
- **THEN** every field equals the original value

#### Scenario: Sink diagnostics round-trip and format

- **WHEN** the sink's `to_string`, `to_json`, and `std::format("{}", sink)` are evaluated
- **THEN** the text and JSON name the service, application code, published, dropped, and hash-cache counters, the formatter matches `to_string`, and no `from_json` exists for the live sink

### Requirement: Trace transport is optional

The core tracing library SHALL remain usable without iceoryx2. The sink and its wire type SHALL be available only in the communication-enabled build, and enabling them SHALL NOT change the recorded span layout or add work to span recording.

#### Scenario: Core-only build has no iceoryx2 dependency

- **WHEN** the project is configured with the communication module disabled
- **THEN** the tracing library and its in-process sinks build without iceoryx2 and without the transport headers

#### Scenario: Recording hot path is unchanged

- **WHEN** the sink is enabled and spans are recorded on producer threads
- **THEN** span recording performs no conversion, hashing, or transport work on those threads
