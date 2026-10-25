# Spec Delta

## MODIFIED Requirements

### Requirement: Monotonic nanosecond stamping

Each accepted record SHALL carry a timestamp from `nexigen::chrono`'s monotonic nanosecond clock domain (CLOCK_MONOTONIC_RAW; a calibrated cycle-counter fast path is permitted where it is provably equivalent), and successive stamps observed on one producer SHALL never go backwards.

#### Scenario: Stamps are non-decreasing per producer

- **WHEN** a stream of records is produced and drained in FIFO order
- **THEN** each record's timestamp is greater than or equal to its predecessor's

#### Scenario: Stamps reflect real elapsed time

- **WHEN** a known busy-wait interval separates two records
- **THEN** their timestamp difference is within a small tolerance of the interval's true duration
