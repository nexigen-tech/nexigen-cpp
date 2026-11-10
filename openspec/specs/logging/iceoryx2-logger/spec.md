# logging/iceoryx2-logger Specification

## Purpose

Defines the asynchronous logger that publishes fixed-size log frames to iceoryx2 instead of writing files: the producer hot path stays on the local wait-free ring, the background worker publishes each rendered frame over the publish-subscribe + event transport, so the platform can later aggregate logs in a central daemon.

## Requirements

### Requirement: Log frame is a fixed zero-copy frame

`nexigen::logging` SHALL publish each log record as a fixed-size, cache-line-aligned, trivially-copyable `LogFrame` carrying a monotonic `timestamp_ns`, an `app_id`, a `context_id`, a `thread_id`, a `pid`, a `log_level`, a `payload_size`, a `dropped_counter`, and a fixed `payload` buffer. The frame SHALL contain no pointers, no owning members, and no heap allocation, so it survives a zero-copy shared-memory exchange unchanged.

#### Scenario: Frame layout is fixed and pointer-free

- **WHEN** `LogFrame` is instantiated
- **THEN** `sizeof(LogFrame)` equals the documented fixed size, `alignof(LogFrame)` is a multiple of the cache line, and `std::is_trivially_copyable_v<LogFrame>` holds

#### Scenario: Frame carries the metadata fields

- **WHEN** a worker packs a rendered message into a frame
- **THEN** the frame's `timestamp_ns`, `app_id`, `thread_id`, `pid`, `log_level`, `payload_size`, and `dropped_counter` are set to the record's values, `pid` equals the client process's OS PID, and the `payload` holds the rendered message bytes

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

`Iceoryx2LoggerConfig` SHALL carry the transport and worker parameters: the iceoryx2 service name, the maximum subscriber count and history depth, the worker thread attributes (core affinity), the worker idle-poll ceiling, the initial log level, and the throttle policy. Invalid values SHALL be rejected before the worker starts.

#### Scenario: Config round-trips and renders

- **WHEN** a populated `Iceoryx2LoggerConfig` passes through `to_json` then `from_json`
- **THEN** every field is recovered, `to_string()` and `std::format("{}", cfg)` name the service and the worker settings, and the formatter output equals the member output

#### Scenario: Invalid config is rejected

- **WHEN** the service name is empty or oversized, or the worker thread spec is invalid
- **THEN** initialization fails with `InvalidArgument` and no worker thread is started

### Requirement: Iceoryx2 logger diagnostics

`Iceoryx2Logger` SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization reporting the service name, level threshold, pending count, drop counts, and worker state, mirroring the other async loggers.

#### Scenario: Logger renders its state

- **WHEN** `to_string()` and `std::format("{}", logger)` are evaluated after initialization
- **THEN** the text names the service and the worker state, and the formatter output equals the member output