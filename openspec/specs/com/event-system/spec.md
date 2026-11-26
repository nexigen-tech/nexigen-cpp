# com/event-system Specification

## Purpose

Defines 1:N event delivery over iceoryx2 with transport sequence integrity, loss and restart reporting, hardware-originated metadata, bounded no-allocation production, and multiplexed consumer waiting.

## Requirements

### Requirement: Event header and timestamp provenance

`EventHeader` SHALL be an 80-byte, trivially copyable, 8-byte-aligned value carrying transport
sequence, the event instant in its native time domain, the domain identifier, the domain epoch, the
canonical projection when one is available, producer epoch, host ingest time, optional hardware
sequence and raw ticks, canonical event code, optional raw hardware event code, presence flags, and
a 16-byte application code. Native hardware time SHALL be preserved as raw ticks;
`ingest_timestamp_ns` SHALL be host monotonic raw.

#### Scenario: Header layout is pinned

- **WHEN** the event-system headers are compiled
- **THEN** `EventHeader` is exactly 80 bytes, 8-byte aligned, and trivially copyable

#### Scenario: Optional hardware fields are explicit

- **WHEN** an FPGA event lacks a hardware sequence, ticks, or event code
- **THEN** the corresponding presence flag is false and the value is not interpreted

#### Scenario: Timestamp domains are distinguished

- **WHEN** an FPGA event carries a hardware instant
- **THEN** the header names the hardware domain, carries that domain's epoch and raw ticks,
  and no host-derived nanosecond value is presented as the event instant

#### Scenario: Canonical projection is optional

- **WHEN** a consumer holds a domain map for the event's domain and epoch
- **THEN** it may derive the canonical instant from the raw ticks, and the header's
  canonical projection is used only when the producer marked it available

#### Scenario: Software event time is host-domain

- **WHEN** an event is published without an explicit event time
- **THEN** its instant and `ingest_timestamp_ns` come from the same host clock sample and
  the domain identifier names the canonical host domain

#### Scenario: Application code travels with the header

- **WHEN** an event is published with application code `CN_LOOP_1`
- **THEN** the consumer reads `CN_LOOP_1` from `app_id_text()` and an unstamped producer reads as
  empty

#### Scenario: Sixteen-byte identifier travels whole

- **WHEN** an event is published with a 16-byte application code
- **THEN** the consumer reads all 16 bytes without truncation

### Requirement: Event payload contract

`EventChannel<T>`, `EventProducer<T>`, and `EventConsumer<T, OnEvent, OnLoss>` payloads SHALL require `EventPayload<T>`, defined as `com::concepts::Message<T>`: a complete trivially copyable object type. `EventConsumerSet` SHALL accept only consumers instantiated from `EventPayload` types. Non-trivially-copyable, reference, and void types SHALL be rejected at compile time; `EmptyPayload` SHALL be accepted.

#### Scenario: POD payload is accepted

- **WHEN** a plain struct of arithmetic members is used as an event payload
- **THEN** `EventPayload<T>` is satisfied and the channel compiles

#### Scenario: Non-POD payload is a compile error

- **WHEN** an event payload contains `std::string`, a container, or a virtual function
- **THEN** compilation fails at the `EventPayload` constraint

#### Scenario: Empty payload is supported

- **WHEN** `EmptyPayload` is used as the payload type
- **THEN** the channel compiles and published events report `has_payload = false`

### Requirement: Channel configuration and limits

`ChannelConfig` SHALL define `max_consumers` in `1..16`, `max_publishers` in `1..16` (default 1), `queue_size` (default 512), `history_size` (default 16, not exceeding the queue), and a default timestamp domain. Invalid values SHALL be rejected before any service is created. `EventChannel<T>` SHALL use 16 consumers, one publisher, 512/16 queue/history when no configuration is supplied.

#### Scenario: Defaults match the event contract

- **WHEN** an `EventChannel<T>` is created without a configuration
- **THEN** it requests at most 16 consumers, one publisher, a 512-sample queue, and 16 history samples

#### Scenario: Invalid limits are rejected

- **WHEN** `max_consumers` is zero or greater than 16
- **THEN** channel creation returns `InvalidArgument`

#### Scenario: Invalid publisher limit is rejected

- **WHEN** `max_publishers` is zero or greater than 16
- **THEN** channel creation returns `InvalidArgument`

#### Scenario: History cannot exceed the queue

- **WHEN** `history_size` is greater than `queue_size`
- **THEN** channel creation returns `InvalidArgument`

#### Scenario: Service limits follow the configuration

- **WHEN** a channel is created with 16 consumers and one publisher
- **THEN** the data service requests 16 subscribers, 1 publisher, 17 nodes, and safe overflow with history 16

#### Scenario: Two publishers share one service

- **WHEN** a status channel is created with `max_publishers=2`
- **THEN** the data service accepts two publishers and 18 nodes, and both producers can publish

### Requirement: Event production and publish outcome

`EventProducer<T>` SHALL publish through the zero-copy `com` publisher with `EventHeader`, stamp
the configured application code, assign the transport sequence only after a successful send, sample
the host ingest timestamp, preserve optional hardware metadata, and return
`PublishOutcome::Notified` or `PublishOutcome::NotifyFailed` when the sample was sent. A failed
send SHALL return a `com::Error` and SHALL NOT consume a sequence number; `notify()` SHALL retry
the wake without republishing.

#### Scenario: Publish consumes one transport sequence

- **WHEN** an event is published successfully
- **THEN** the header carries the current sequence, the sequence advances by one, and the outcome is `Notified`

#### Scenario: Failed send does not consume a sequence

- **WHEN** the transport cannot send a sample
- **THEN** the call returns a `com::Error` and the next successful publish reuses the same sequence

#### Scenario: Notify failure is distinct from publish failure

- **WHEN** the sample is sent but the event notification fails
- **THEN** the outcome is `NotifyFailed`, the sequence still advances, and a later `notify()` can retry the wake

#### Scenario: Hardware metadata is preserved

- **WHEN** a gateway publishes an FPGA event with hardware sequence, timestamp, and event code
- **THEN** all three values and their presence flags reach the consumer unchanged

#### Scenario: Empty payload is published

- **WHEN** an event is published with `EmptyPayload`
- **THEN** the header reports `has_payload = false` and the event is delivered normally

#### Scenario: Producer stamps the application code

- **WHEN** a producer is created with application code `LIDR`
- **THEN** every published header carries `LIDR` in `app_id`

### Requirement: Producer epoch and source restart

`producer_epoch` SHALL identify the gateway lifecycle. Creation SHALL assign a nonzero unique epoch; `on_source_reset()` SHALL replace the epoch and reset the transport sequence to zero on the publishing thread only. Consumers SHALL treat an epoch change as `ProducerRestart`. A channel SHALL accept at most one publisher.

#### Scenario: Source reset starts a new lifecycle

- **WHEN** the gateway calls `on_source_reset()` after an FPGA restart
- **THEN** subsequent events carry a new epoch and start again at transport sequence zero

#### Scenario: Normal publication keeps the epoch

- **WHEN** events are published without a source reset
- **THEN** every event keeps the same epoch and a monotonically increasing sequence

#### Scenario: Consumer reports producer restart

- **WHEN** a consumer observes a different epoch
- **THEN** it reports `ProducerRestart`, resets its expected sequence, and accepts the new event

#### Scenario: Second publisher is rejected

- **WHEN** a second producer tries to open the same channel
- **THEN** service creation or publisher creation fails instead of accepting a second writer

### Requirement: Transport loss detection and callbacks

`EventConsumer` SHALL track the transport sequence. The first sample SHALL establish the baseline without a loss callback; `seq > expected` SHALL report `TransportGap` with the exact lost count; `seq < expected` (nonzero) SHALL report `Duplicate` and drop the sample; `seq == 0` with `expected > 1` SHALL report `UnexpectedSequenceReset` and accept the sample. Epoch changes SHALL report `ProducerRestart`. Statistics SHALL count every case.

#### Scenario: First sample establishes the baseline

- **WHEN** a consumer receives its first sample, including one obtained from history
- **THEN** it records `first_sequence`, computes `history_skipped`, and does not call the loss callback

#### Scenario: Gap reports the exact loss

- **WHEN** the expected sequence is 10 and the next sample is 13
- **THEN** the loss callback receives `TransportGap` with `lost_count = 3` and the sample is delivered

#### Scenario: Duplicate is dropped

- **WHEN** a sample's sequence is lower than expected and not zero
- **THEN** the loss callback receives `Duplicate`, the sample is not delivered, and the expected sequence is unchanged

#### Scenario: Unexpected sequence reset is recovered

- **WHEN** a sample has sequence zero while the same epoch expects a value greater than one
- **THEN** the loss callback receives `UnexpectedSequenceReset`, the expected sequence resets to one, and the sample is delivered

#### Scenario: Producer restart is reported

- **WHEN** the producer epoch changes
- **THEN** the loss callback receives `ProducerRestart` with the old and new epoch, and no exact lost count is reported

#### Scenario: Statistics reflect every outcome

- **WHEN** events are received, lost, duplicated, and reset
- **THEN** `EventConsumerStats` reports received, transport-lost, duplicate, unexpected-reset, restart, first and last sequence counters

### Requirement: Consumer waiting and multiplexing

`EventConsumer` SHALL expose non-blocking `drain()` and blocking `wait()` without timeout or wake-up. `EventConsumerSet<MaxConsumers>` SHALL own one wait set, attach consumers, expose `wait()`, `wait_for(timeout)`, and `wake_up()`, return `Events`, `Timeout`, or `WokenUp`, and support at most 16 consumers with fixed storage. Attach, detach, and wait SHALL run on one thread; a consumer SHALL NOT be simultaneously standalone-waiting and set-attached.

#### Scenario: Single consumer waits without a timeout

- **WHEN** an `EventConsumer` calls `wait()` and a producer notifies it
- **THEN** the call drains the queue and returns success without polling

#### Scenario: Set wakes on any consumer

- **WHEN** two consumers are attached to a set and only one receives an event
- **THEN** the set drains the fired consumer and returns `Events`

#### Scenario: Timeout is a normal outcome

- **WHEN** `wait_for()` expires before any event arrives
- **THEN** the set returns `Timeout` rather than an error

#### Scenario: Wake-up interrupts the wait

- **WHEN** another thread calls `wake_up()` while the set is waiting
- **THEN** the wait returns `WokenUp` promptly and pending data remains available for a later drain

#### Scenario: Consumer modes are exclusive

- **WHEN** a consumer is attached to a set and its standalone `wait()` is called
- **THEN** the call returns `InvalidState` instead of waiting on two mechanisms

#### Scenario: Capacity is fixed

- **WHEN** the 17th consumer is attached to an `EventConsumerSet<16>`
- **THEN** attach returns `OutOfSamples` without allocating

#### Scenario: Idle wait does not spin

- **WHEN** a set with no fired consumers waits
- **THEN** the calling thread sleeps in the kernel and consumes no busy-poll CPU

### Requirement: Hardware gateway ingress

Hardware-originated events SHALL enter through `ISR -> SPSC ring -> gateway thread -> EventProducer`. The ISR SHALL NOT call iceoryx2 or allocate; the driver SHALL preserve FPGA ticks unchanged and SHALL NOT convert them to nanoseconds; the gateway SHALL preserve the hardware sequence, raw ticks, domain identifier, domain epoch, and raw event code. `hardware_sequence` SHALL be trace-only and SHALL NOT participate in loss detection. `on_source_reset()` SHALL be called only after the ring has been quiesced.

#### Scenario: FPGA metadata crosses the gateway

- **WHEN** a driver descriptor carries raw ticks, a domain identifier, a domain epoch, a raw event code, and an optional hardware sequence
- **THEN** the published header carries the same values with the correct presence flags

#### Scenario: Driver does not convert ticks

- **WHEN** the driver builds a descriptor from an FPGA event
- **THEN** the timestamp field holds the counter value unchanged and no frequency or
  offset is applied in the driver

#### Scenario: Non-monotonic hardware sequence is trace-only

- **WHEN** hardware sequence values arrive out of order or with gaps
- **THEN** no loss callback is raised and transport sequence remains the loss authority

#### Scenario: Source reset follows ring quiescence

- **WHEN** the gateway detects an FPGA restart and quiesces the SPSC ring
- **THEN** it calls `on_source_reset()` before publishing any event from the new hardware
  lifecycle, and events from the new lifecycle carry the new domain epoch

#### Scenario: ISR path is allocation-free

- **WHEN** the ISR pushes a descriptor into the SPSC ring after initialization
- **THEN** the push performs no heap allocation and does not call iceoryx2

### Requirement: Event channel factory

`EventChannel<T>` SHALL be an application-facing factory that creates only the requested endpoint: `producer()` creates a publisher and notifier, `consumer(callbacks)` creates a subscriber and listener. It SHALL apply the same `ChannelConfig` to both endpoints, SHALL not create unused ports, and SHALL accept either inline template callbacks or `std::function` callables. The `Node` SHALL outlive the channel and its endpoints.

#### Scenario: Minimal channel uses defaults

- **WHEN** an application calls `EventChannel<T>::create(node, name)`
- **THEN** producer and consumer use the 16/512/16 defaults

#### Scenario: Producer creates no subscriber

- **WHEN** only `producer()` is called
- **THEN** no subscriber or listener port is created for that channel

#### Scenario: Consumer creates no publisher

- **WHEN** only `consumer(callbacks)` is called
- **THEN** no publisher or notifier port is created for that channel

#### Scenario: std::function callbacks are accepted

- **WHEN** callbacks are supplied as `std::function`
- **THEN** the channel accepts them through the same factory without a separate API

### Requirement: Event-system diagnostics and serialization

`TimestampDomain`, `LossReason`, `PublishOutcome`, and `WaitOutcome` SHALL provide `to_string`, `from_string`, non-throwing JSON, and `std::formatter`. `EventHeader`, `ChannelConfig`, `EventTime`, `HardwareMetadata`, `LossInfo`, `EventProducerStats`, and `EventConsumerStats` SHALL provide `to_string`, formatter, and non-throwing JSON. `EventChannel`, `EventProducer`, `EventConsumer`, and `EventConsumerSet` SHALL provide cold-path `to_string` and formatter diagnostics.

#### Scenario: Enums round-trip

- **WHEN** each event-system enum value passes through `to_string`, `from_string`, JSON, and `std::format`
- **THEN** every canonical name round-trips and hostile JSON leaves the target unchanged

#### Scenario: Value structs serialize

- **WHEN** each event-system value struct is rendered and round-tripped through JSON
- **THEN** diagnostics are deterministic and JSON parsing under `JSON_NOEXCEPTION=1` never throws

#### Scenario: Runtime objects render diagnostics

- **WHEN** `to_string()` and `std::format("{}", object)` are evaluated on a channel, producer, consumer, or consumer set
- **THEN** both render the object's cold-path state

### Requirement: Wait-set consumers consume fired listener activations

When a wait set reports a consumer or wake attachment as fired, `EventConsumerSet` SHALL consume that listener activation before or while draining the consumer. A consumed activation with no newly delivered samples SHALL NOT be reported as `Events`, and transport readiness SHALL NOT remain set after the wait returns.

#### Scenario: Fired consumer activation is consumed
- **WHEN** a producer publishes once, the set drains it, and the consumer then waits again with no new data
- **THEN** the second wait consumes the pending listener activation and returns `Timeout` without busy-polling

#### Scenario: Wake activation is consumed
- **WHEN** `wake_up()` returns `WokenUp` and the next wait has no new data
- **THEN** the next wait consumes the pending wake activation and returns `Timeout` without busy-polling

#### Scenario: Idle wait after traffic does not spin
- **WHEN** a consumer has delivered one event and the set waits 100 ms with no new data
- **THEN** the calling thread consumes less than 50 ms of CPU during that wait

#### Scenario: Activation consumption failure is surfaced
- **WHEN** consuming a fired listener activation fails
- **THEN** `wait()` or `wait_for()` returns the mapped error instead of reporting fake events

#### Scenario: Wake flag is authoritative
- **WHEN** a stale wake activation is consumed without a pending `wake_up()`
- **THEN** the wait does not report `WokenUp`

### Requirement: Multi-publisher sequence tracking

When `max_publishers` is greater than one, `EventConsumer` SHALL track the transport sequence and producer epoch independently per `app_id`. Interleaved publishers SHALL NOT report `ProducerRestart`. An epoch change for one `app_id` SHALL report `ProducerRestart`; gaps, duplicates, and unexpected resets SHALL be detected per publisher. With `max_publishers=1`, the single-sequence behaviour SHALL apply unchanged.

#### Scenario: Interleaved publishers do not restart

- **WHEN** two producers with different application codes alternately publish on a channel created with `max_publishers=2`
- **THEN** the consumer delivers every sample without calling the loss callback

#### Scenario: One publisher restarts

- **WHEN** one of the two producers changes its epoch
- **THEN** the loss callback receives `ProducerRestart` for that publisher with the old and new epoch

#### Scenario: Gaps stay per publisher

- **WHEN** one publisher skips a transport sequence while the other publisher keeps publishing in between
- **THEN** the loss callback receives `TransportGap` with that publisher's exact lost count

#### Scenario: New publisher establishes its own baseline

- **WHEN** a previously unseen application code publishes on a multi-publisher channel
- **THEN** its first sample establishes that publisher's expected sequence without a loss callback

#### Scenario: Single-publisher channel is unchanged

- **WHEN** the channel is created with `max_publishers=1` and the producer epoch changes
- **THEN** the loss callback still receives `ProducerRestart` with the old and new epoch
