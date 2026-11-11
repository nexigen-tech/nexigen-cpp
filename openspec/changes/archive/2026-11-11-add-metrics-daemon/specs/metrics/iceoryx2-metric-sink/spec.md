# Spec Delta

## Purpose

Defines the client-side sink that publishes harvested metric batches to iceoryx2 as fixed frames, so a central daemon can collect and export them across processes.

## ADDED Requirements

### Requirement: MetricFrame wire type

`MetricFrame` SHALL be a fixed-size, trivially-copyable type that carries the publishing application's 4-byte identifier, the publisher PID, the window timestamp, and exactly one `MetricRecord`. It SHALL be the sole cross-process contract between the sink and the daemon.

#### Scenario: Frame is a fixed-size POD

- **WHEN** `sizeof(MetricFrame)`, `alignof(MetricFrame)`, and `std::is_trivially_copyable_v<MetricFrame>` are evaluated
- **THEN** the type is trivially copyable, its size is fixed and independent of the record count, and its alignment does not exceed the cache-line alignment of its members

#### Scenario: Frame serialization round-trip

- **WHEN** a `MetricFrame` carrying an `app_id`, `pid`, timestamp, and a `MetricRecord` is serialized with `to_json` and parsed back with `from_json` under `JSON_NOEXCEPTION=1`
- **THEN** the `app_id`, `pid`, timestamp, and the embedded record survive the round-trip unchanged

#### Scenario: Frame diagnostics contract

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a `MetricFrame`
- **THEN** the member and free results are equal non-empty strings, and the formatter renders the same text

### Requirement: Iceoryx2 metric sink satisfies the sink concept

`Iceoryx2MetricSink` SHALL satisfy `concepts::MetricSink` by offering `on_batch(const MetricBatchSnapshot&) noexcept -> core::Status` and `flush() noexcept -> core::Status` with no virtual dispatch.

#### Scenario: Sink satisfies the concept

- **WHEN** `concepts::MetricSink<Iceoryx2MetricSink>` is evaluated
- **THEN** it is `true` in a constant expression

#### Scenario: Sink callbacks are allocation-free

- **WHEN** `on_batch` runs under an armed allocation arena
- **THEN** no `operator new`, `delete`, `malloc`, `free`, or dynamic container growth occurs

### Requirement: Batches are published as frames

On `on_batch`, `Iceoryx2MetricSink` SHALL wrap every record in the delivered batch in a `MetricFrame`, publish each frame over its iceoryx2 pub-sub service, and notify listeners at most once per delivered batch. A transport that is out of samples SHALL drop frames without blocking the harvester.

#### Scenario: Every record becomes a frame

- **WHEN** a batch of N records is delivered to the sink
- **THEN** N frames are published, each carrying the window timestamp and one record, and the app identifier and PID are stamped identically on every frame

#### Scenario: Listeners are notified once per batch

- **WHEN** a non-empty batch is delivered to the sink
- **THEN** the event notifier fires once for that batch, and a subsequent drained subscriber observes all N frames

#### Scenario: Out-of-samples does not block

- **WHEN** the pub-sub service has no free sample and `on_batch` is invoked
- **THEN** the call returns without blocking the harvester thread and without allocating

### Requirement: Sink flush is a no-op

`Iceoryx2MetricSink::flush()` SHALL return a successful `core::Status` because every published frame is already durable in shared memory at publish time.

#### Scenario: Flush succeeds

- **WHEN** `flush()` is called on the sink
- **THEN** it returns a successful `core::Status` without performing I/O

### Requirement: Iceoryx2 metric sink configuration

`Iceoryx2MetricSinkConfig` SHALL carry the publishing application's 4-byte identifier, the iceoryx2 service name to publish to, and the subscriber buffer depth the daemon needs to receive one publish burst without discards. Its values SHALL round-trip through `to_json`/`from_json` under `JSON_NOEXCEPTION=1`.

#### Scenario: Config serialization round-trip

- **WHEN** an `Iceoryx2MetricSinkConfig` carrying an app identifier, service name, and subscriber buffer depth is serialized and parsed back
- **THEN** all fields survive the round-trip under `JSON_NOEXCEPTION=1`

#### Scenario: Config diagnostics contract

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to an `Iceoryx2MetricSinkConfig`
- **THEN** the member and free results are equal non-empty strings, and the formatter renders the same text

### Requirement: Sink diagnostics

`Iceoryx2MetricSink` SHALL render its subscription service and application identifier through `to_string` and `std::formatter`, and SHALL NOT be deserializable, because rebinding a sink performs I/O at construction.

#### Scenario: Sink renders its state

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a configured `Iceoryx2MetricSink`
- **THEN** the output names the service and the application identifier, and the formatter output equals the member output

#### Scenario: Rebinding is construction, not deserialization

- **WHEN** a translation unit attempts `from_json` into an `Iceoryx2MetricSink`
- **THEN** compilation fails because no overload exists
