# phm/watchdog Specification

## Purpose

Gives the platform health daemon ownership of a hardware watchdog and a feed
policy tied to supervision results, so an unrecoverable supervision or daemon
failure resets the machine instead of leaving it running degraded.

## Requirements

### Requirement: Watchdog ownership and lifecycle

The daemon SHALL own at most one watchdog device. When enabled, it SHALL open the configured device, set the hardware timeout before feeding, and record the granted timeout. On clean shutdown it SHALL disarm with the magic-close sequence so a deliberate stop cannot reset the machine. A failure to open the device while enabled SHALL abort startup with a non-zero exit.

#### Scenario: Startup arms the device

- **WHEN** the manifest enables the watchdog with a 1000 ms timeout
- **THEN** the daemon opens the device, sets the timeout, and reports the granted value before the first feed

#### Scenario: Clean shutdown disarms

- **WHEN** the daemon exits through its normal stop path
- **THEN** it performs the magic close and the machine does not reset

#### Scenario: Missing device aborts startup

- **WHEN** the watchdog is enabled and the device cannot be opened
- **THEN** the daemon reports the error and exits non-zero without entering the monitoring loop

### Requirement: Feed policy

The daemon SHALL feed the watchdog only when every armed supervised process is `Ok` or `Failed` within tolerance, no process is `Expired`, and the monitoring loop completed a cycle within the last feed period. Otherwise it SHALL stop feeding until a healthy cycle occurs.

#### Scenario: Healthy system is fed

- **WHEN** every armed process is within tolerance and monitoring cycles complete on time
- **THEN** the daemon feeds the watchdog once per feed period

#### Scenario: Expired process stops the feed

- **WHEN** any armed process becomes `Expired`
- **THEN** the daemon stops feeding and does not resume while the process stays expired

#### Scenario: Recovery resumes the feed

- **WHEN** the expired process returns to `Ok` before the hardware timeout elapses
- **THEN** the daemon resumes feeding on the next cycle and the machine does not reset

#### Scenario: Stalled monitoring loop stops the feed

- **WHEN** the monitoring loop does not complete a cycle within the feed period
- **THEN** the next feed is skipped

### Requirement: Injectable device boundary

Watchdog device access SHALL be abstracted behind an injectable boundary so unit tests run against an in-memory double, and a real-device integration test SHALL run only when the device exists.

#### Scenario: Unit tests use the double

- **WHEN** the watchdog tests run without a watchdog device
- **THEN** they exercise open, timeout, feed, and magic-close behavior against the double and pass

#### Scenario: Real-device test is skipped when absent

- **WHEN** `/dev/watchdog` does not exist on the test host
- **THEN** the integration test reports skipped instead of failing

### Requirement: Watchdog diagnostics

The watchdog SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization reporting the device path, granted timeout, feed count, and whether feeding is active.

#### Scenario: Watchdog renders its state

- **WHEN** `to_string()` and `std::format("{}", watchdog)` are evaluated after a feed
- **THEN** the text names the device, timeout, feed count, and active state, and both renderings match
