# Spec Delta

## ADDED Requirements

### Requirement: SafeState priority

When the `StateClient` receives a `SafeState` command from the PHM daemon, the EM SHALL process it with the highest priority: any ordinary queued transition SHALL wait, and the SafeState switch SHALL be executed and acknowledged first.

#### Scenario: SafeState overtakes queued transitions

- **WHEN** ordinary transitions are queued and a SafeState command arrives
- **THEN** the EM executes and acknowledges the SafeState switch before the queued ordinary transitions

#### Scenario: Ordinary transitions proceed afterwards

- **WHEN** the SafeState switch has completed
- **THEN** the queued ordinary transitions resume processing in their original order
