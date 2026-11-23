# Spec Delta

## ADDED Requirements

### Requirement: Daemon opens the log service with the shared capacity options

When the daemon is the first participant, it SHALL create the log publish-subscribe service with the shared log-service options: 4 subscribers, 64 publishers, 64 nodes, a 16 384-sample subscriber depth, history 4, and safe overflow enabled. A late publisher or subscriber SHALL observe the same shape. An existing service with incompatible options SHALL be rejected with a diagnostic.

#### Scenario: Daemon installs the shared service shape

- **WHEN** the daemon starts before any logging application and creates the service
- **THEN** the service reports the shared capacities and a later publisher connects successfully

#### Scenario: Late subscriber sees the same depth

- **WHEN** a viewer subscribes after the daemon created the service
- **THEN** the subscriber's maximum buffer depth is 16 384 without recreating the service

#### Scenario: Incompatible live service is rejected

- **WHEN** the daemon starts against a live service created with a 2-sample buffer
- **THEN** startup fails with a diagnostic naming the required and existing depths instead of degrading silently
