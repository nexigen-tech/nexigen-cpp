# Spec Delta

## MODIFIED Requirements

### Requirement: Daemon subscribes and drains log frames

The `LogDaemon` SHALL open a `Subscriber<LogFrame>` on the configured iceoryx2 service and process incoming frames through a `WaitSet` drain loop without busy polling: the daemon thread SHALL sleep in the kernel until a frame arrives or a batch-flush timer fires. The loop SHALL consume the listener activation that woke it before draining the subscriber.

#### Scenario: Frame arrives and is processed

- **WHEN** an `Iceoryx2Logger` publishes a frame
- **THEN** the daemon's subscriber receives it within one backoff window and the frame is handed to the formatter

#### Scenario: Real daemon receives a published frame end-to-end

- **WHEN** a producer publishes through the real iceoryx2 transport and the daemon is running
- **THEN** the daemon writes the formatted frame to its output file

#### Scenario: Idle daemon costs zero CPU

- **WHEN** no frames are published for an extended period
- **THEN** the daemon's service-core utilization is 0% (WaitSet sleep, no polling)

#### Scenario: Notification activation is consumed

- **WHEN** the listener wakes the wait set with no queued frame
- **THEN** the callback drains the activation and the wait set blocks again instead of spinning
