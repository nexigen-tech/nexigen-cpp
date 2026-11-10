# Spec Delta

## MODIFIED Requirements

### Requirement: SafeState escalation

When local recovery fails or a safety-critical component fails, the daemon SHALL escalate by emitting a recovery-notification event to the State Manager (per HLR-SM-030/REQ_SM_007) carrying the failed Function Group; the SM arbitrates the fault and commands the Function Group switch through the `StateClient` interface, or the daemon triggers a hardware watchdog reset. The notification SHALL be an event over the `com` transport.

#### Scenario: Critical failure escalates to SafeState

- **WHEN** a safety-critical process expires
- **THEN** the daemon emits a recovery-notification event for the affected function group to the SM, which commands the SafeState switch through the StateClient interface
