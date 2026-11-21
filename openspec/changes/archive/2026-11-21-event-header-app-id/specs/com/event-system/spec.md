# Spec Delta

## MODIFIED Requirements

### Requirement: Event header and timestamp provenance

`EventHeader` SHALL be a 72-byte, trivially copyable, 8-byte-aligned value carrying transport
sequence, the event instant in its native time domain, the domain identifier, the domain epoch, the
canonical projection when one is available, producer epoch, host ingest time, optional hardware
sequence and raw ticks, canonical event code, optional raw hardware event code, presence flags, and
a four-byte application code. Native hardware time SHALL be preserved as raw ticks;
`ingest_timestamp_ns` SHALL be host monotonic raw.

#### Scenario: Header layout is pinned

- **WHEN** the event-system headers are compiled
- **THEN** `EventHeader` is exactly 72 bytes, 8-byte aligned, and trivially copyable

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

- **WHEN** an event is published with application code `LIDR`
- **THEN** the consumer reads `LIDR` from `app_id_text()` and an unstamped producer reads as empty

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
