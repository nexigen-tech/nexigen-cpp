# chrono/time-sync Specification

## Purpose
Detects violations of the time base the platform relies on: steps, backwards
jumps, and excessive slew of `CLOCK_REALTIME` relative to the monotonic clock,
so timestamped data and distributed deadlines can be trusted.

## Requirements

### Requirement: Time synchronization classification

`nexigen::chrono` SHALL expose a `TimeSyncMonitor` that compares successive
offset samples between `CLOCK_REALTIME` and `CLOCK_MONOTONIC_RAW` and classifies
each transition as `Ok`, `Stepped`, `GoingBackwards`, or `RateExceeded`, using a
configurable policy.

#### Scenario: A normal offset drift is Ok

- **WHEN** the offset moves by less than the maximum step and within the rate limit
- **THEN** the sample reports `Ok` with the measured step and rate

#### Scenario: A forward step is detected

- **WHEN** the offset jumps forward by more than the maximum step
- **THEN** the sample reports `Stepped`

#### Scenario: A backwards jump is detected

- **WHEN** the offset jumps backwards by more than the maximum step
- **THEN** the sample reports `GoingBackwards`

#### Scenario: A fast slew is detected

- **WHEN** the offset drifts faster than the configured ppm over at least the minimum window
- **THEN** the sample reports `RateExceeded`

### Requirement: Threshold policy

The monitor SHALL accept a `TimeSyncPolicy` with `max_step_ns`,
`max_rate_ppm`, and `min_window_ns`. The defaults SHALL be 1 ms, 500 ppm, and
100 ms; the rate SHALL NOT be evaluated below the minimum window.

#### Scenario: The minimum window suppresses rate noise

- **WHEN** two samples are taken closer than `min_window_ns`
- **THEN** the rate check is skipped and only the step check applies

#### Scenario: A custom policy is honoured

- **WHEN** a monitor is constructed with a policy of 100 us and 50 ppm
- **THEN** classification uses those thresholds

### Requirement: Monitor diagnostics

The status and the sample SHALL provide `to_string` plus `std::formatter`
surfaces reporting the offset, step, and rate.

#### Scenario: A sample renders its values

- **WHEN** `to_string()` and `std::format("{}", sample)` are evaluated
- **THEN** both name the status, offset, step, and rate, and the outputs match
