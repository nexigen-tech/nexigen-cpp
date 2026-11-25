# Spec Delta

## ADDED Requirements

### Requirement: Wait-set consumers consume fired listener activations

When a wait set reports a consumer or wake attachment as fired, `EventConsumerSet` SHALL consume that listener activation before or while draining the consumer. A consumed activation with no newly delivered samples SHALL NOT be reported as `Events`, and transport readiness SHALL NOT remain set after the wait returns.

#### Scenario: Fired consumer activation is consumed
- **WHEN** a producer publishes once, the set drains it, and the consumer then waits again with no new data
- **THEN** the second wait consumes the pending listener activation and returns `Timeout` without busy-polling

#### Scenario: Wake activation is consumed
- **WHEN** `wake_up()` returns `WokenUp` and the next wait has no new data
- **THEN** the next wait consumes the pending wake activation and returns `Timeout` without busy-polling

#### Scenario: Idle wait after traffic does not spin
- **WHEN** a consumer has delivered one event and the set waits 100 ms with no new data
- **THEN** the calling thread consumes less than 50 ms of CPU during that wait

#### Scenario: Activation consumption failure is surfaced
- **WHEN** consuming a fired listener activation fails
- **THEN** `wait()` or `wait_for()` returns the mapped error instead of reporting fake events

#### Scenario: Wake flag is authoritative
- **WHEN** a stale wake activation is consumed without a pending `wake_up()`
- **THEN** the wait does not report `WokenUp`
