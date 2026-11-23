# logging/iceoryx2-logger Specification

## Purpose

Defines the asynchronous logger that publishes fixed-size log frames to iceoryx2 instead of writing files: the producer hot path stays on the local wait-free ring, the background worker publishes each rendered frame over the publish-subscribe + event transport, so the platform can later aggregate logs in a central daemon.

## Requirements

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

### Requirement: Worker publishes frames off the hot path

The logger SHALL render and publish frames exclusively on its background worker thread: the producer hot path SHALL only pack arguments and enqueue to the local shard ring, never formatting, never touching the transport, and never allocating. Each rendered record SHALL be published over iceoryx2 publish-subscribe and followed by an event notification so a subscriber wakes without polling.

#### Scenario: Producer path stays allocation-free

- **WHEN** a producer thread emits a record in a measured loop
- **THEN** the producer path performs no heap allocation and its median latency remains within the module's ns-class gate

#### Scenario: A frame reaches a subscriber

- **WHEN** a subscriber is connected and a producer emits a record
- **THEN** the subscriber receives the frame with the rendered message and metadata

#### Scenario: Publication works with zero subscribers

- **WHEN** no subscriber is connected
- **THEN** `send()` and the event `notify()` complete without error and the publisher keeps operating

### Requirement: Drop count crosses the process boundary

The logger SHALL carry its local drop accounting into the frames: when the shard ring overflows, the worker SHALL place the local dropped count into the next successfully published frame and then reset the counter, so a downstream daemon observes the loss without side-band communication.

#### Scenario: Overflow is reported in the next frame

- **WHEN** a burst overflows the producer's shard ring and a subsequent frame is published
- **THEN** that frame's `dropped_counter` equals the number of dropped records since the previous frame, and the next frame carries zero

### Requirement: Iceoryx2 logger configuration

`Iceoryx2LoggerConfig` SHALL carry the transport and worker parameters: the iceoryx2 service name, the maximum subscriber count and history depth, the worker thread attributes (core affinity), the worker idle-poll ceiling, the initial log level, and the throttle policy. The `max_subscribers` and `history_size` values SHALL equal the shared log-service constants so an application cannot require a service shape that a running daemon created with different values. Invalid values SHALL be rejected before the worker starts.

#### Scenario: Config round-trips and renders

- **WHEN** a populated `Iceoryx2LoggerConfig` passes through `to_json` then `from_json`
- **THEN** every field is recovered, `to_string()` and `std::format("{}", cfg)` name the service and the worker settings, and the formatter output equals the member output

#### Scenario: Invalid config is rejected

- **WHEN** the service name is empty or oversized, or the worker thread spec is invalid
- **THEN** initialization fails with `InvalidArgument` and no worker thread is started

#### Scenario: Default capacity matches the shared shape

- **WHEN** a default `Iceoryx2LoggerConfig` is validated
- **THEN** `max_subscribers` is 4 and `history_size` is 4

#### Scenario: Diverging capacity is rejected

- **WHEN** `max_subscribers` is set to a value other than 4, or `history_size` to a value other than 4
- **THEN** initialization returns `InvalidArgument` and no worker thread starts

### Requirement: Log service capacity is a shared contract

The logger SHALL create the log publish-subscribe service with the shared log-service options: at most 4 subscribers, 64 publishers, 64 nodes, a subscriber buffer depth of 16 384 samples, history depth 4, and safe overflow enabled. A publish SHALL NOT fail because a subscriber lags; the transport SHALL evict the oldest unread sample instead.

#### Scenario: Publisher installs the shared service shape

- **WHEN** an `Iceoryx2Logger` initializes on a service that does not exist yet
- **THEN** the created service reports 4 subscribers, 64 publishers, 64 nodes, a 16 384-sample subscriber depth, history 4, and safe overflow enabled

#### Scenario: A third logging application connects

- **WHEN** three applications publish log frames on the same service
- **THEN** every publisher is created successfully and all three streams reach the subscriber

#### Scenario: Slow subscriber does not fail the publisher

- **WHEN** a subscriber stops draining while the publisher sends more than 16 384 frames
- **THEN** every send completes without `ReceiveBufferFull`, the oldest unread frames are evicted, and the next received frame reports the gap through `lost_before`

#### Scenario: Existing incompatible service is rejected

- **WHEN** the logger starts while a live service was created with different capacity options
- **THEN** initialization fails with a diagnostic naming the required and the existing values

### Requirement: Iceoryx2 logger diagnostics

`Iceoryx2Logger` SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization reporting the service name, level threshold, pending count, drop counts, and worker state, mirroring the other async loggers.

#### Scenario: Logger renders its state

- **WHEN** `to_string()` and `std::format("{}", logger)` are evaluated after initialization
- **THEN** the text names the service and the worker state, and the formatter output equals the member output
