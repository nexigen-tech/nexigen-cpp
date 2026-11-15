# phm/recovery Specification

## Purpose

Defines the PHM recovery actions: local process restart over a PHM→EM request-response channel, global SafeState escalation through the StateClient interface, and non-blocking fault storage.

## Requirements

### Requirement: Local process recovery

When a non-critical process's supervision status becomes `Expired`, the daemon SHALL send a restart request carrying the process identity (`pid`/name) to the Execution Manager over a request-response channel, without a serialization step. An absent, rejected, or timed-out answer SHALL count as a failed local recovery, and the daemon SHALL escalate instead of retrying indefinitely.

#### Scenario: Expired process is restarted

- **WHEN** a non-critical process expires and the daemon sends a restart request
- **THEN** the Execution Manager receives the request identifying that process

#### Scenario: Rejected restart escalates

- **WHEN** the Execution Manager answers that the restart was rejected
- **THEN** the daemon treats local recovery as failed and applies the escalation path

#### Scenario: Missing Execution Manager escalates

- **WHEN** no Execution Manager answers within the recovery timeout
- **THEN** the daemon treats local recovery as failed and applies the escalation path

### Requirement: SafeState escalation

When local recovery fails or a safety-critical component fails, the daemon SHALL emit a recovery-notification event to the State Manager carrying the failed Function Group; the SM arbitrates the fault and commands the Function Group switch through the `StateClient` interface. When escalation requires a reset, the daemon SHALL perform it itself by ceasing to feed the watchdog it owns, without a request to the Execution Manager.

#### Scenario: Critical failure escalates to SafeState

- **WHEN** a safety-critical process expires
- **THEN** the daemon emits a recovery-notification event for the affected function group to the SM, which commands the SafeState switch through the StateClient interface

#### Scenario: Watchdog reset is local actuation

- **WHEN** an escalation rule selects `TriggerWatchdogReset`
- **THEN** the daemon stops feeding its watchdog and sends no recovery request to the Execution Manager

### Requirement: Recovery action vocabulary

`nexigen::phm` SHALL expose a `RecoveryAction` scoped enum with the values `RestartProcess`, `SwitchFunctionGroupState`, and `TriggerWatchdogReset`, with the full project conversion set (`to_string`/`from_string`, ADL `to_json`/`from_json`, and `std::formatter`).

#### Scenario: RecoveryAction round-trips

- **WHEN** each `RecoveryAction` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders the name

### Requirement: Fault storage

The daemon SHALL record each detected violation (fault type, timestamp, process id, checkpoint id) to non-volatile storage asynchronously on a dedicated infrastructure thread, without blocking the monitoring loop.

#### Scenario: Fault is recorded without blocking

- **WHEN** a violation is detected
- **THEN** the fault record is written by the storage thread and the monitoring loop continues without waiting

### Requirement: Recovery diagnostics

The recovery module SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization reporting the last recovery action taken.

#### Scenario: Recovery renders its last action

- **WHEN** `to_string()` and `std::format("{}", recovery)` are evaluated after a recovery action
- **THEN** the text names the action and the formatter output equals the member output

### Requirement: Fault log record integrity

Each appended fault record SHALL carry a monotonic sequence number and a CRC-32C
over its fields. A verification scan SHALL classify every line as intact,
corrupted, truncated, or legacy, SHALL accept pre-change four-field lines as
legacy and unverified, and SHALL NOT abort on malformed input.

#### Scenario: Written records verify

- **WHEN** records are written and the log is scanned
- **THEN** every record is reported intact with a strictly increasing sequence number

#### Scenario: A flipped byte is detected

- **WHEN** one byte of a record is changed on disk and the log is scanned
- **THEN** that record is reported corrupted and the scan continues with the next line

#### Scenario: A truncated tail is reported

- **WHEN** the final line is cut before its checksum
- **THEN** it is reported truncated, not corrupted, and no previous record is affected

#### Scenario: Legacy records stay readable

- **WHEN** the log contains pre-change four-field lines
- **THEN** they are reported legacy and unverified, and the scan exits successfully
