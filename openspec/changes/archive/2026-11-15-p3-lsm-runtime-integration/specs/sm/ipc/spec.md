# Spec Delta

## ADDED Requirements

### Requirement: LSM-bound TriggerIn execution

The SM arbitration loop SHALL select the LSM for a bound group and run its
transition table from the same single writer thread. The selection cost SHALL be
a small in-memory lookup, with no new thread or synchronization.

#### Scenario: LSM runs in the arbitration loop

- **WHEN** a TriggerIn request arrives for a bound group
- **THEN** the LSM executes in the arbitration callback

#### Scenario: No binding keeps the existing service

- **WHEN** no LSM is bound to the requested group
- **THEN** the existing TriggerIn arbitration path runs unchanged
