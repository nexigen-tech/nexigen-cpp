# Spec Delta

## MODIFIED Requirements

### Requirement: Event header and timestamp provenance

`EventHeader` SHALL be a 64-byte, trivially copyable, 8-byte-aligned value carrying transport sequence, the event instant in its native time domain, the domain identifier, the domain epoch, the canonical projection when one is available, producer epoch, host ingest time, optional hardware sequence and raw ticks, canonical event code, optional raw hardware event code, and presence flags. Native hardware time SHALL be preserved as raw ticks; `ingest_timestamp_ns` SHALL be host monotonic raw.

#### Scenario: Header layout is pinned

- **WHEN** the event-system headers are compiled
- **THEN** `EventHeader` is exactly 64 bytes, 8-byte aligned, and trivially copyable

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
