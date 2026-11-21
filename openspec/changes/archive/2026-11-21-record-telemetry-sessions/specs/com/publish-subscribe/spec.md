# Spec Delta

## ADDED Requirements

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
