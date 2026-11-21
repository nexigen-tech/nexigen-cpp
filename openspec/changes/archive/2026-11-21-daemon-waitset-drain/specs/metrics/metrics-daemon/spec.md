# Spec Delta

## MODIFIED Requirements

### Requirement: Daemon subscribes and drains frames without polling

The `MetricsDaemon` SHALL open a `Subscriber<MetricFrame>` on the configured iceoryx2 service and process incoming frames through a `WaitSet` drain loop, sleeping in the kernel until a frame arrives or a batch timer fires. The loop SHALL consume the listener activation that woke it before draining the subscriber.

#### Scenario: Frame arrives and is processed

- **WHEN** an `Iceoryx2MetricSink` publishes frames
- **THEN** the daemon's subscriber receives them within one backoff window and hands them to the aggregator

#### Scenario: Idle daemon costs zero CPU

- **WHEN** no frames are published for an extended period
- **THEN** the daemon's service-core utilization is 0% (WaitSet sleep, no polling)

#### Scenario: Notification activation is consumed

- **WHEN** the listener wakes the wait set with no queued frame
- **THEN** the callback drains the activation and the wait set blocks again instead of spinning
