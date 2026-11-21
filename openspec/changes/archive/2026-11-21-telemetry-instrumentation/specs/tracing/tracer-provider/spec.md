# Spec Delta

## ADDED Requirements

### Requirement: Meter counters for started and dropped spans

`TracerProvider` SHALL take the meter as its last template parameter, defaulted to
`core::NullMeter`, and create counters for `started` and for each drop cause:
`dropped.rate_limited`, `dropped.buffer_full`, and `dropped.slot_exhausted`. The existing
`dropped()` snapshot SHALL remain unchanged, and a `NullMeter` provider SHALL contain no counter
code.

#### Scenario: Started span increments the counter

- **WHEN** a sampled span is started
- **THEN** `started` advances by one

#### Scenario: Each drop cause has its own counter

- **WHEN** a span is dropped by sampling, a full shard, or slot exhaustion
- **THEN** only the matching `dropped.*` counter advances

#### Scenario: Snapshot and counters agree

- **WHEN** a test produces a known number of spans and drops per cause
- **THEN** the cumulative counters and the `dropped()` snapshot report the same totals

#### Scenario: Null meter removes counter code

- **WHEN** a provider is instantiated without an explicit meter
- **THEN** it uses `core::NullMeter` and the span counters compile away
