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

### Requirement: Platform sync mode

`nexigen::chrono` SHALL expose `SyncMode` with `Standalone`, `PtpSynced`, and
`Holdover`, and the published block SHALL carry the active mode. `Standalone`
names the local CPU oscillator/RTC as master, `PtpSynced` names an external
IEEE 1588 grandmaster reached through the NIC PHC, and `Holdover` names the last
locked local-oscillator drift model.

#### Scenario: Standalone is the no-PTP mode

- **WHEN** no PTP stack or NIC PHC is available
- **THEN** the mode is `Standalone` and the base can still be `Ready`

#### Scenario: PTP lock reports PtpSynced

- **WHEN** `CLOCK_TAI` is disciplined by the external grandmaster and the step
  and slew checks pass
- **THEN** the mode is `PtpSynced`

#### Scenario: Loss transitions through Holdover

- **WHEN** PTP lock is lost after a `PtpSynced` interval
- **THEN** the mode becomes `Holdover` for the configured window before becoming
  `Standalone` or degrading required TAI domains

#### Scenario: Mode round-trips

- **WHEN** every `SyncMode` value passes through `to_string`, `from_string`,
  `std::format`, and non-throwing JSON
- **THEN** the canonical spelling round-trips

### Requirement: Time-service availability flags

The block and each snapshot SHALL carry a `ServiceFlags` bitmask reporting
observed presence and health, not process presence: `phc_present`,
`tai_available`, `host_counter_available`, `host_counter_synchronized`,
`ptp4l_required`, `phc2sys_required`, `ptp_stack_ready`, `tai_synchronized`,
`step_detected`, `slew_active`, `rt_sched_applied`, `mlock_applied`, and
`declared_domains_complete`.

#### Scenario: Missing services are visible

- **WHEN** a required PTP service or the NIC PHC is absent
- **THEN** `ptp_stack_ready` is clear while `ptp4l_required` or
  `phc2sys_required` remains set, and the daemon keeps running in `Standalone`

#### Scenario: Flags reflect observed state

- **WHEN** the daemon samples `CLOCK_TAI` and `adjtimex`
- **THEN** `phc_present`, `tai_available`, `tai_synchronized`,
  `step_detected`, and `slew_active` reflect the observed kernel/device state,
  not a configured assumption

#### Scenario: Diagnostics expose flags

- **WHEN** the block or a snapshot is rendered
- **THEN** `to_string()` and `std::format` name the active flags without
  allocation on a producer path
