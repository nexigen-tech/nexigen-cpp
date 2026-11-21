# Spec Delta

## ADDED Requirements

### Requirement: Blocking collection without polling

The collector SHALL block in a `com::WaitSet` until a stream notification or the periodic
maintenance interval fires, and SHALL NOT poll the transport on a fixed short cycle. Each enabled
log, metric, and trace service SHALL have a wake-up listener on its companion `<service>Event`
service; each event service SHALL use its own channel notification. The maintenance interval SHALL
drive `TimeSync` and bound shutdown latency.

#### Scenario: Idle collector does not poll

- **WHEN** no producer publishes for several maintenance intervals
- **THEN** the collector wakes only on the maintenance interval, writes no records, and does not
  busy-poll the streams

#### Scenario: Publication wakes the collector

- **WHEN** a producer publishes a log frame and notifies the companion event service
- **THEN** the blocked collector wakes and records the frame before the next maintenance interval

#### Scenario: Disabled stream has no attachment

- **WHEN** trace capture is disabled
- **THEN** the collector does not attach a trace wake-up listener

#### Scenario: SIGTERM stops within one interval

- **WHEN** SIGTERM arrives while the collector is blocked
- **THEN** it drains queued records, writes the footer, and exits within one maintenance interval
