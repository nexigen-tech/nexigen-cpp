# Spec Delta

## ADDED Requirements

### Requirement: Meter counters for accepted and dropped records

`ConsoleLogger`, `FileLogger`, and `Iceoryx2Logger` SHALL take the meter as their last template
parameter, defaulted to `core::NullMeter`, and create counters for `accepted` and for each drop
cause: `dropped.rate_limited`, `dropped.buffer_full`, and `dropped.slot_exhausted`. The existing
`dropped()` snapshot SHALL remain unchanged, and a `NullMeter` logger SHALL contain no counter
code.

#### Scenario: Accepted record increments the counter

- **WHEN** a record is accepted into the producer shard
- **THEN** `accepted` advances by one

#### Scenario: Each drop cause has its own counter

- **WHEN** a record is dropped by the throttle, by a full shard, or by slot exhaustion
- **THEN** only the matching `dropped.*` counter advances

#### Scenario: Snapshot and counters agree

- **WHEN** a test drops a known number of records per cause
- **THEN** the cumulative counters and the `dropped()` snapshot report the same totals

#### Scenario: Null meter removes counter code

- **WHEN** a logger is instantiated without an explicit meter
- **THEN** it uses `core::NullMeter` and the drop counters compile away
