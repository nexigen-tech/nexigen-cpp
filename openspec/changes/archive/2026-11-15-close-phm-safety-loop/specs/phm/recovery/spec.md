# Spec Delta

## MODIFIED Requirements

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
