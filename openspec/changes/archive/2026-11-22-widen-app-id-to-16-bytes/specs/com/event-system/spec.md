# Spec Delta

## MODIFIED Requirements

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
