# Spec Delta

## ADDED Requirements

### Requirement: Meter counters for provider failures

`chrono::Provider` SHALL take the meter as its last template parameter, defaulted to
`core::NullMeter`, and create counters for `calibration_failed`, `acquire_exhausted`, and
`snapshot_failed`. A `NullMeter` provider SHALL contain no counter code, and the existing return
statuses and snapshot diagnostics SHALL remain unchanged.

#### Scenario: Calibration failure increments the counter

- **WHEN** `prepare()` fails to calibrate at least one bank source
- **THEN** `calibration_failed` advances by one

#### Scenario: Exhausted bank increments the counter

- **WHEN** `acquire()` is called after every source is issued
- **THEN** `acquire_exhausted` advances by one and the call still returns `ResourceExhausted`

#### Scenario: Snapshot failure increments the counter

- **WHEN** `snapshot_now()` fails because the clock or the sink reports an error
- **THEN** `snapshot_failed` advances by one and the error is still returned

#### Scenario: Null meter removes counter code

- **WHEN** a provider is instantiated without an explicit meter
- **THEN** it uses `core::NullMeter` and the failure counters compile away
