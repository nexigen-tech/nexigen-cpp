# Spec Delta

## ADDED Requirements

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
