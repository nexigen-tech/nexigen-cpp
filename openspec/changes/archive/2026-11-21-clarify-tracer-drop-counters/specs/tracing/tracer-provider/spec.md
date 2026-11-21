# Spec Delta

## MODIFIED Requirements

### Requirement: Meter counters for started and dropped spans

`TracerProvider` SHALL take the meter as its last template parameter, defaulted to
`core::NullMeter`, and create counters for `started`, `dropped.buffer_full`, and
`dropped.slot_exhausted`. `dropped.rate_limited` SHALL remain in the `dropped()` snapshot only,
because it is counted inside `Tracer::start_span`. The existing `dropped()` snapshot SHALL remain
unchanged, and a `NullMeter` provider SHALL contain no counter code.

#### Scenario: Started span increments the counter

- **WHEN** a sampled span is started
- **THEN** `started` advances by one

#### Scenario: Each drop cause has its own counter

- **WHEN** a span is dropped by a full shard or by slot exhaustion
- **THEN** only the matching `dropped.*` counter advances

#### Scenario: Rate-limited drops stay in the snapshot

- **WHEN** the span-rate budget rejects a span
- **THEN** `dropped().rate_limited` advances and no metric counter is written

#### Scenario: Snapshot and counters agree

- **WHEN** a test produces a known number of spans and metered drops per cause
- **THEN** the cumulative counters and the corresponding `dropped()` fields report the same totals

#### Scenario: Null meter removes counter code

- **WHEN** a provider is instantiated without an explicit meter
- **THEN** it uses `core::NullMeter` and the span counters compile away
