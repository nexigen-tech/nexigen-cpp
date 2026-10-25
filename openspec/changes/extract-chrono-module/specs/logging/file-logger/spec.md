# Spec Delta

## MODIFIED Requirements

### Requirement: Nanosecond UTC timestamps via calibrated clocks

`FileLogger` SHALL calibrate the offset between `CLOCK_REALTIME` and `CLOCK_MONOTONIC_RAW` at initialization through `nexigen::chrono`'s UTC-offset calibration, and the writer thread SHALL convert each record's monotonic timestamp through that calibration to produce the UTC ISO-8601 value with nanosecond digits using `nexigen::chrono`'s time formatting, without calling `CLOCK_REALTIME` on the hot path.

#### Scenario: File timestamps track wall time

- **WHEN** a record is written shortly after a known `CLOCK_REALTIME` reading
- **THEN** its JSON timestamp differs from that wall reading only by the expected drain/format latency (sub-millisecond tolerance on an idle system)

#### Scenario: Order within file matches monotonic order

- **WHEN** consecutive records are drained
- **THEN** their formatted timestamps are non-decreasing
