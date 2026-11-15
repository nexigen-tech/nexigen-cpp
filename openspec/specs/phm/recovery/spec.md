# phm/recovery Specification

## Purpose

Defines the PHM recovery actions: local process restart over a PHM→EM request-response channel, global SafeState escalation through the StateClient interface, and non-blocking fault storage.

## Requirements

### Requirement: Local process recovery

When a non-critical process's supervision status becomes `Expired`, the daemon SHALL send a restart request carrying a PHM-generated `request_id`, the process identity (`pid`/name), and the process's Function Group to the Execution Manager over a request-response channel, without a serialization step. The answer SHALL mean "admitted for restart", not "restarted": the daemon SHALL treat an absent, timed-out, or `Rejected` answer as a failed local recovery and escalate, while an `Accepted` answer ends the daemon's participation in that attempt.

#### Scenario: Expired process is restarted

- **WHEN** a non-critical process expires and the daemon sends a restart request
- **THEN** the Execution Manager receives the request identifying that process, and the daemon stops waiting once it answers `Accepted`

#### Scenario: Rejected restart escalates

- **WHEN** the Execution Manager answers `Rejected` with a reason
- **THEN** the daemon treats local recovery as failed, records the reason, and applies the escalation path

#### Scenario: Missing Execution Manager escalates

- **WHEN** no Execution Manager answers within the response timeout
- **THEN** the daemon treats local recovery as failed and applies the escalation path

#### Scenario: Accepted restart is not treated as completed

- **WHEN** the Execution Manager answers `Accepted` and the process is restarted later
- **THEN** the daemon reports no recovery failure and takes no further action for that attempt
### Requirement: SafeState escalation

When local recovery fails or a safety-critical component fails, the daemon SHALL emit a recovery-notification event to the State Manager carrying the failed Function Group and the request correlation id; the SM arbitrates the fault and commands the Function Group switch through the `StateClient` interface. The daemon SHALL address the notification to the Function Group the process belongs to, never to the process name, and SHALL NOT drop a notification silently.

#### Scenario: Critical failure escalates to SafeState

- **WHEN** a safety-critical process expires
- **THEN** the daemon emits a recovery-notification event naming that process's Function Group and the State Manager commands the SafeState switch through the StateClient interface

#### Scenario: Watchdog reset is local actuation

- **WHEN** an escalation rule selects `TriggerWatchdogReset`
- **THEN** the daemon stops feeding its watchdog and sends no recovery request to the Execution Manager

#### Scenario: Escalation never uses a process name as a group

- **WHEN** a recovery rule is exhausted or names no explicit group
- **THEN** the notification carries the Function Group resolved for that process

#### Scenario: Unknown group is reported, not dropped

- **WHEN** the daemon has no Function Group for the failing process
- **THEN** the escalation is recorded as a fault naming the unknown group and the watchdog escalation path applies instead of discarding the notification
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

### Requirement: Recovery outcome vocabulary

`nexigen::phm` SHALL expose a `RecoveryOutcome` scoped enum with the values `Accepted`, `InProgress`, and `Rejected`, and a `RecoveryReason` scoped enum naming why a request was not accepted, including `None`, `UnknownProcess`, `UnknownGroup`, `NotOperational`, `AlreadyRestarting`, `InvalidAction`, `NoSafeState`, `EmUnavailable`, `EmRejected`, and `SystemError`. Both SHALL carry the full project conversion set.

#### Scenario: Outcome and reason round-trip

- **WHEN** every `RecoveryOutcome` and `RecoveryReason` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders the name

#### Scenario: Rejection always carries a reason

- **WHEN** a recovery answer reports `Rejected`
- **THEN** its reason is one of the enumerated causes and never `None`

### Requirement: Recovery request correlation

The daemon SHALL generate a monotonically increasing `request_id` for every recovery it starts and SHALL carry it on both the Execution Manager request and the State Manager notification, so that the acknowledgement of one recovery cannot satisfy the wait for another.

#### Scenario: Concurrent recoveries are distinguished

- **WHEN** two processes in different Function Groups expire before either acknowledgement arrives
- **THEN** each acknowledgement is matched to its own recovery by `request_id`

#### Scenario: Stale acknowledgement is ignored

- **WHEN** an acknowledgement arrives for a `request_id` the daemon is no longer waiting on
- **THEN** the daemon ignores it and keeps waiting for the current one

### Requirement: Missing acknowledgement escalates to the watchdog

After notifying the State Manager the daemon SHALL wait for a `RecoveryAck` for at most `recovery_timeout`. An acknowledgement reporting `Rejected`, an `InProgress` acknowledgement that is not followed by `Accepted` within the same budget, and an absent acknowledgement SHALL each stop watchdog feeding and fire the watchdog reaction.

#### Scenario: Rejected acknowledgement fires the watchdog

- **WHEN** the State Manager acknowledges with `Rejected`
- **THEN** the daemon stops feeding and fires the watchdog reaction

#### Scenario: Absent acknowledgement fires the watchdog

- **WHEN** no acknowledgement arrives within `recovery_timeout`
- **THEN** the daemon stops feeding and fires the watchdog reaction

#### Scenario: InProgress buys no extra time

- **WHEN** the State Manager acknowledges with `InProgress` and does not follow up with `Accepted` before `recovery_timeout` expires
- **THEN** the daemon stops feeding and fires the watchdog reaction

#### Scenario: Accepted acknowledgement keeps the platform alive

- **WHEN** the State Manager acknowledges with `Accepted` within `recovery_timeout`
- **THEN** the daemon keeps feeding and leaves further action to the State Manager
