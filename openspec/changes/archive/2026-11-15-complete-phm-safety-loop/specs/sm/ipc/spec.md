# Spec Delta

## ADDED Requirements

### Requirement: Recovery acknowledgement channel

The SM SHALL publish `RecoveryAck` samples on `Nexigen/SM/RecoveryAck` and ring `Nexigen/SM/RecoveryAckEvent` once per acknowledged recovery notification. The message SHALL be fixed-size and trivially copyable, and the publication SHALL occur on the arbitration thread with no additional thread or synchronization.

#### Scenario: Acknowledgement reaches the PHM daemon

- **WHEN** the SM acknowledges a recovery notification
- **THEN** the sample is published on the acknowledgement channel and the paired wake event is rung

#### Scenario: No PHM subscriber is harmless

- **WHEN** the SM publishes an acknowledgement and no PHM daemon is subscribed
- **THEN** the publication succeeds and arbitration continues unaffected
