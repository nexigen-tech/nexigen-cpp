# Spec Delta

## ADDED Requirements

### Requirement: Optional identity claim from the supervised entity

A supervised entity SHALL accept an optional claim configuration (sample
channel, wake event, and re-claim interval). When configured, it SHALL publish
its name, pid, and real uid during creation and at most once per interval while
checkpoints are reported. A failed initial claim SHALL fail creation so the
application does not run unverified.

#### Scenario: Creation claims the identity

- **WHEN** an application creates an entity with a claim configuration
- **THEN** the claim is published and the paired wake event fires

#### Scenario: A periodic claim repairs a lost one

- **WHEN** the daemon was not subscribed when the claim was first published
- **THEN** the next interval re-publishes it and the daemon verifies the process

#### Scenario: On-demand claim is available

- **WHEN** the application calls `claim_identity()` explicitly
- **THEN** the claim is published immediately and the retry interval restarts
