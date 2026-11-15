# Spec Delta

## ADDED Requirements

### Requirement: Pending transitions carry a request id

The SM SHALL allocate a monotonically increasing request id for each accepted
TriggerIn transition, store it in the pending transition, and reuse it for every
`InProgress` retry. On `OperationCanceled`, the SM SHALL drop the pending entry
without forcing a `SafeState` fallback, because the request was superseded
rather than failed. The SM MachineFG lifecycle client SHALL maintain its own
request-id sequence.

#### Scenario: InProgress retries reuse the id

- **WHEN** the EM acknowledges `InProgress` for a pending transition
- **THEN** the next retry carries the same request id

#### Scenario: Cancellation drops the pending transition

- **WHEN** the EM acknowledges `OperationCanceled` for a pending transition
- **THEN** the SM removes it and does not force `SafeState`

#### Scenario: MachineFG requests use their own ids

- **WHEN** the SM commands MachineFG during startup
- **THEN** those commands carry ids from the lifecycle client's sequence
