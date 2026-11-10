# phm/recovery Specification

## Purpose

Defines the PHM recovery actions: local process restart over a PHM→EM request-response channel, global SafeState escalation through the StateClient interface, and non-blocking fault storage.

## Requirements

### Requirement: Local process recovery

When a non-critical process's supervision status becomes `Expired`, the daemon SHALL send a high-priority restart request for that process to the Execution Manager over a request-response channel, carrying the process identity (`pid`/name); the request SHALL be delivered without a serialization step.

#### Scenario: Expired process is restarted

- **WHEN** a non-critical process expires and the daemon sends a restart request
- **THEN** the Execution Manager receives the request identifying that process

### Requirement: SafeState escalation

When local recovery fails or a safety-critical component fails, the daemon SHALL escalate by sending a `SafeState` (function-group state switch) command through the `StateClient` interface, or by triggering a hardware watchdog reset; the command SHALL be a typed request over the `com` request-response transport.

#### Scenario: Critical failure escalates to SafeState

- **WHEN** a safety-critical process expires
- **THEN** the daemon sends a `SafeState` command for the affected function group through the StateClient interface

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
