# com/publish-subscribe Specification

## Purpose

Defines the platform's zero-copy Publish-Subscribe transport: `Publisher<T>` writes messages in place into shared memory and `Subscriber<T>` reads them as read-only views of the same pages, with Event notification, a compile-time trivially-copyable payload contract, non-blocking overflow handling, and subscriber-lag telemetry.

## Requirements

### Requirement: Zero-copy publication

`nexigen::com::Publisher<T, H = void>` SHALL let the producer borrow a shared-memory slot through `loan_uninit()` returning `SampleMut<T, H>`, construct the payload in place, and hand it over with `send()`. The payload SHALL never be serialized, copied between buffers, or heap-allocated: the subscriber observes the very bytes the producer wrote. When the service was opened with a non-void custom header type `H`, `SampleMut<T, H>::user_header_mut()` SHALL give mutable access to the header before `send()`.

#### Scenario: Loan and send delivers the message

- **WHEN** a publisher loans a sample, writes fields into it, and sends it to a connected subscriber
- **THEN** the subscriber receives a sample whose bytes equal the values the producer wrote

#### Scenario: Loan without send returns the slot

- **WHEN** a producer loans a sample and drops it without calling `send()`
- **THEN** the slot returns to the shared pool and the next `loan_uninit()` reuses it without pool exhaustion

#### Scenario: Custom header is written before send and read after receive

- **WHEN** a publisher and subscriber are created with a non-void custom header type `H`, the publisher writes into `user_header_mut()` before `send()`, and the subscriber reads `user_header()` after `receive()`
- **THEN** the subscriber observes the exact same header bytes the publisher wrote

### Requirement: Subscriber receive

`nexigen::com::Subscriber<T, H = void>` SHALL expose `receive()` returning a `Sample<T, H>` that is a read-only view of the shared-memory slot (the same physical page the producer wrote), and the view SHALL be released for reuse when the caller drops it. When `H` is not `void`, `Sample<T, H>::user_header()` SHALL give const read access to the custom header that the publisher attached to that sample.

#### Scenario: Received sample is the published value

- **WHEN** a subscriber calls `receive()` after a publisher sent a sample
- **THEN** the returned `Sample<T, H>` reports exactly the values that were sent, and no copy was made to deliver them

#### Scenario: Released sample returns to the pool

- **WHEN** a subscriber drops its `Sample<T, H>` after reading it
- **THEN** the shared slot becomes available to the producer again and the queue fill level drops accordingly

#### Scenario: Subscriber reads custom header

- **WHEN** a publisher sends a sample with a custom header set to known values
- **THEN** the subscriber's `receive()` returns a sample whose `user_header()` carries the same values

### Requirement: Event notification on publish

Publishing a sample SHALL notify waiting listeners through the iceoryx2 Event mechanism: `nexigen::com::EventNotifier` pairs with a publisher and `nexigen::com::EventListener` wakes without busy polling.

#### Scenario: Listener wakes on publish

- **WHEN** a listener is blocked in `wait()` and a publisher sends a sample
- **THEN** the listener wakes promptly with the notification, without any polling loop

### Requirement: Trivially-copyable message contract

Every `com` port template SHALL require `com::concepts::Message<T>`, an alias of `core::concepts::Message<T>`, which is satisfied exactly when `std::is_trivially_copyable_v<T>` is true, so payloads are plain bytes with no pointers, no virtual tables, and no owning members; a violating type SHALL be rejected at compile time.

#### Scenario: POD payload is accepted

- **WHEN** a plain struct of arithmetic members is used as a publisher payload type
- **THEN** the `Message<T>` constraint holds and the publisher compiles

#### Scenario: Non-POD payload is a compile error

- **WHEN** a negative-compile probe instantiates a publisher over a type containing `std::string`, `std::vector`, a smart pointer, or a virtual function
- **THEN** compilation fails at the constraint, not at runtime

#### Scenario: Alias resolves to the core concept

- **WHEN** `com::concepts::Message<T>` and `core::concepts::Message<T>` are compared for any `T`
- **THEN** both concepts evaluate identically

### Requirement: Compile-time sizing and pool allocation

Port creation SHALL derive the payload size and alignment from `T` itself (`sizeof(T)`/`alignof(T)`) and request a shared-memory pool sized from the configured `max_subscribers` and `max_samples_per_subscriber`; the developer never writes byte counts.

#### Scenario: Pool follows the payload type

- **WHEN** a publisher for `T` is created with `max_subscribers` and `max_samples_per_subscriber`
- **THEN** the shared-memory segment is sized to hold exactly that many samples of `sizeof(T)` with `alignof(T)` alignment

### Requirement: Type-layout mismatch detection

A subscriber connecting to an existing service SHALL verify the publisher's payload type name, size, and alignment; on mismatch the connection SHALL fail with `com::Error::IncompatibleTypeLayout` instead of delivering corrupt bytes.

#### Scenario: Mismatched layouts are rejected

- **WHEN** a subscriber compiled against a different version of `T` connects to a live publisher
- **THEN** the connect returns `IncompatibleTypeLayout` and no sample is exchanged

### Requirement: Non-blocking overflow

Publishing SHALL be non-blocking: when a subscriber's queue is full the delivery policy SHALL discard the oldest unread sample and the publisher SHALL keep running in `O(1)` without waiting on any subscriber.

#### Scenario: Slow subscriber cannot block the publisher

- **WHEN** a subscriber stops reading and its queue fills up while the publisher keeps sending
- **THEN** the oldest unread sample is evicted per send, every `send()` completes immediately, and the publisher never blocks

### Requirement: Subscriber lag telemetry

The send path SHALL maintain per-subscriber diagnostics: a queue-fill counter, a lost-sample counter, and a fill threshold; when the threshold is crossed or a sample is evicted, the module SHALL increment the counters and report the condition through the diagnostics surface.

#### Scenario: Counters track fill and loss

- **WHEN** a subscriber reads slowly enough that its queue crosses the configured threshold and later overflows
- **THEN** the fill counter reflects the queue level, the lost counter reflects every evicted sample, and `to_string()` on the port names both values

### Requirement: Port diagnostics

`Publisher`, `Subscriber`, and `EventNotifier`/`EventListener` SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization naming the service and the port state.

#### Scenario: Ports render their state

- **WHEN** `to_string()` and `std::format("{}", port)` are evaluated on a created publisher and subscriber
- **THEN** both render the service name and the current fill level, and the formatter output equals the member output

### Requirement: Configurable subscriber buffer depth

`Publisher::create` and `Subscriber::create` SHALL accept an optional `subscriber_max_buffer_size` that sets the service's maximum subscriber buffer depth. A value of `0` SHALL preserve the transport default, and a non-zero value SHALL be applied to the service so a subscriber can hold that many samples without deterministic discards.

#### Scenario: A full publish burst survives

- **WHEN** a publisher creates a service with a subscriber maximum of N and publishes N frames before the subscriber drains
- **THEN** a subscriber opened with the same maximum and a buffer of N receives all N frames without a discard

#### Scenario: Zero preserves the transport default

- **WHEN** `create` is called with `subscriber_max_buffer_size = 0`
- **THEN** the service keeps the transport default depth and existing callers behave unchanged

#### Scenario: Existing zero-argument callers are unaffected

- **WHEN** an existing caller invokes `create(node, name)` without the new option
- **THEN** the call compiles and behaves exactly as before

### Requirement: Single-call publication of a caller-owned payload

`nexigen::com::Publisher<T, H>` SHALL provide `send_copy(const T&)` that publishes a caller-owned payload in one transport call, equivalent to loaning a slot, writing the payload once, and sending it. The call SHALL be non-blocking under the existing discard policy, SHALL allocate no heap memory, and SHALL update the same published and lost-sample telemetry as `send()`.

#### Scenario: Payload is delivered unchanged

- **WHEN** a publisher calls `send_copy()` with a payload and a subscriber is connected to the same service
- **THEN** the subscriber receives one sample whose bytes equal the payload exactly

#### Scenario: Lost-sample snapshot spans the call

- **WHEN** a subscriber's queue is full and `lost_samples()` is read before and after one `send_copy()` call
- **THEN** the second reading is greater, so the caller can count the evicted sample

#### Scenario: Publication is allocation-free

- **WHEN** an armed allocation counter observes `send_copy()` calls after the publisher was created
- **THEN** zero heap allocations occur

#### Scenario: Transport refusal is reported, not blocked

- **WHEN** the transport cannot provide a slot or cannot deliver the sample
- **THEN** `send_copy()` returns an error status promptly and never waits on a subscriber

#### Scenario: Diagnostics stay consistent

- **WHEN** `to_string()` and `std::format("{}", publisher)` are evaluated after successful `send_copy()` calls
- **THEN** the published count includes those calls and the formatter output equals the member output

### Requirement: Publish-subscribe service options

`nexigen::com::Publisher` and `Subscriber` SHALL accept an optional `PublishSubscribeOptions` containing `max_subscribers`, `max_publishers`, `max_nodes`, `subscriber_max_buffer_size`, `subscriber_buffer_size`, `history_size`, `subscriber_history_request`, and `enable_safe_overflow`. The options SHALL be applied when the service is created and verified when an existing service is opened. Existing overloads SHALL preserve the current transport defaults and behaviour.

#### Scenario: Existing callers keep the old defaults

- **WHEN** a caller creates a publisher or subscriber without options
- **THEN** it uses the same transport settings and compiles with the same call shape as before

#### Scenario: Safe overflow and history settings are applied

- **WHEN** options request safe overflow and a history size
- **THEN** the created service reports those settings

#### Scenario: Subscriber and publisher limits are applied

- **WHEN** options request 16 subscribers, one publisher, and 17 nodes
- **THEN** the service admits that many endpoints and rejects the next one beyond the limit

#### Scenario: Incompatible options are rejected

- **WHEN** two endpoints open the same service with incompatible service-level options
- **THEN** the second open fails instead of silently using different settings

#### Scenario: Buffer depth still works

- **WHEN** options set a subscriber buffer depth
- **THEN** that subscriber can hold that many samples without deterministic discards

### Requirement: Event service options

`nexigen::com::EventNotifier` and `EventListener` SHALL accept an optional `EventOptions` containing `max_listeners`, `max_notifiers`, and `max_nodes`. The options SHALL be applied when the event service is created and verified when it is opened. Existing overloads SHALL preserve the transport defaults.

#### Scenario: Event listener limit is applied

- **WHEN** options request 16 listeners
- **THEN** sixteen listeners can attach and the next listener creation fails

#### Scenario: Event notifier limit is applied

- **WHEN** options request one notifier
- **THEN** a second notifier for the same event service is rejected

#### Scenario: Event node limit is applied

- **WHEN** options request a specific node count
- **THEN** the event service enforces that node limit

#### Scenario: Existing event callers are unchanged

- **WHEN** a caller creates a notifier or listener without options
- **THEN** the call compiles and uses the transport defaults

### Requirement: Service option serialization

`PublishSubscribeOptions` and `EventOptions` SHALL provide `to_string`, `std::formatter`, and non-throwing JSON under `JSON_NOEXCEPTION=1`. A malformed JSON document SHALL leave the destination unchanged.

#### Scenario: Options round-trip

- **WHEN** each option struct is serialized and parsed back
- **THEN** every field keeps its value and the formatter output equals `to_string()`

#### Scenario: Hostile JSON is rejected safely

- **WHEN** a non-object or mistyped JSON value is parsed into an option struct
- **THEN** parsing does not throw and the destination keeps its previous values

### Requirement: Transport sequence header

Telemetry publish-subscribe services SHALL attach a `TransportHeader` to every sample carrying the
application code, the process id, a `publisher_id` unique to each publisher construction, and a
`sequence` that increments on every send. Each subscriber SHALL report how many samples were missed
before the one it received. A first sample or a new `publisher_id` SHALL start a fresh baseline.

#### Scenario: Sequence increments per send

- **WHEN** one publisher sends three samples on a telemetry service
- **THEN** the received headers carry three consecutive sequence values from the same publisher id

#### Scenario: Subscriber reports a gap

- **WHEN** a subscriber's queue overflows and it next receives a sample whose sequence skipped N
  values
- **THEN** the sample reports N missed samples before it

#### Scenario: First sample is a baseline

- **WHEN** a subscriber connects after a publisher has already sent samples
- **THEN** its first received sample reports no gap

#### Scenario: Publisher restart starts a new baseline

- **WHEN** a publisher process restarts and publishes again
- **THEN** the new publisher id starts a sequence baseline and the restart is not reported as a gap

#### Scenario: Header carries publisher identity

- **WHEN** a sample is received from application `LIDR`
- **THEN** its header carries `LIDR` and the publisher's process id

#### Scenario: Producer loss total is carried

- **WHEN** a publisher has failed to deliver three samples and then sends another
- **THEN** its `publish_failed` counter has advanced by three and no wire field is required

#### Scenario: Non-telemetry services keep the default header

- **WHEN** a `com` service outside logging, metrics, and tracing is inspected
- **THEN** it still uses the default header type and its layout is unchanged

### Requirement: Raw introspection subscriber

`com` SHALL provide a raw subscriber that opens an existing publish-subscribe service by name,
reads the service's payload and user-header type details, and receives payload and user-header
bytes without a compile-time payload type. It SHALL be read-only and SHALL NOT create services.

#### Scenario: Subscriber matches the service type

- **WHEN** a raw subscriber opens a service published as `LogFrame` with `TransportHeader`
- **THEN** it receives samples whose payload and user-header sizes match the service's type details

#### Scenario: Unknown service is rejected

- **WHEN** a raw subscriber names a service that does not exist
- **THEN** creation fails with a mapped `com` error and no port is left behind

#### Scenario: Payload stays opaque

- **WHEN** the raw subscriber receives an event payload of an application-defined type
- **THEN** it exposes the bytes and the payload type name without interpreting the payload

#### Scenario: Raw subscription does not allocate per sample

- **WHEN** samples are received in a loop
- **THEN** each receive performs no heap allocation and returns borrowed storage valid until the
  next receive

### Requirement: Endpoint counters from the meter

`Publisher<T, H, M>`, `Subscriber<T, H, M>`, `EventProducer<T, M>`, and
`EventConsumer<T, OnEvent, OnLoss, M>` SHALL constrain `M` with `core::MeterLike`, default it to
`core::NullMeter`, and create their counters at construction: `published`, `publish_failed`,
`evicted` for a publisher; `received`, `lost` for a subscriber; `published`, `publish_failed` for an
event producer; `received`, `lost`, `duplicates` for an event consumer. A `NullMeter` endpoint
SHALL contain no counter code.

#### Scenario: Counters track endpoint activity

- **WHEN** a publisher sends five samples, one of them fails, and one delivery evicts a slow
  subscriber's sample
- **THEN** `published` is 4, `publish_failed` is 1, and `evicted` is 1

#### Scenario: Subscriber counts received and lost

- **WHEN** a subscriber receives two samples after a gap of three
- **THEN** `received` grows by two and `lost` grows by three

#### Scenario: Event duplicates are counted

- **WHEN** an event consumer observes a duplicate sequence
- **THEN** `duplicates` advances and `received` does not

#### Scenario: Default meter disables counters

- **WHEN** an endpoint is instantiated without an explicit meter
- **THEN** it uses `core::NullMeter` and records nothing

#### Scenario: Counters leave through the metric sink

- **WHEN** the application's metric provider harvests
- **THEN** the endpoint counters appear as ordinary metric frames on its configured sink

#### Scenario: Meter is the last template argument

- **WHEN** a publisher or event endpoint is declared with a meter
- **THEN** the meter is its last template parameter, after the header and callbacks

#### Scenario: Meter is endpoint-scoped

- **WHEN** two publishers of the same type are created with the same meter
- **THEN** the second counter creation fails with `InvalidArgument`, so each endpoint receives its
  own meter named after its service

#### Scenario: Counter concurrency follows the meter

- **WHEN** an endpoint may record from several threads
- **THEN** the supplied meter returns counters safe for concurrent `add`

### Requirement: Transport header serialization

`TransportHeader` SHALL provide a cold-path `to_string()`, a `std::formatter` specialization, and
non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. Ill-typed JSON SHALL leave the
destination unchanged.

#### Scenario: Header round-trips

- **WHEN** a header is rendered to JSON and parsed back
- **THEN** application code, process id, publisher id, and sequence keep their values

#### Scenario: Header is diagnosed

- **WHEN** `to_string()` and `std::format` render a header
- **THEN** the text names the application, process id, publisher id, and sequence
