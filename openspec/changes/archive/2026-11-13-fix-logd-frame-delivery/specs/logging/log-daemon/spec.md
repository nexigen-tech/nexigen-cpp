# Spec Delta

## MODIFIED Requirements

### Requirement: Daemon subscribes and drains log frames

The `LogDaemon` SHALL open a `Subscriber<LogFrame>` on the configured iceoryx2 service and process incoming frames through a `WaitSet` drain loop without busy polling: the daemon thread SHALL sleep in the kernel until a frame arrives or a batch-flush timer fires.

#### Scenario: Frame arrives and is processed

- **WHEN** an `Iceoryx2Logger` publishes a frame
- **THEN** the daemon's subscriber receives it within one backoff window and the frame is handed to the formatter

#### Scenario: Real daemon receives a published frame end-to-end

- **WHEN** the `nexigen-logd` daemon binary runs against an `Iceoryx2Logger` (or a minimal `LogFrame` publisher) on the same service
- **THEN** the daemon writes the published record to its configured log directory

#### Scenario: Idle daemon costs zero CPU

- **WHEN** no frames are published for an extended period
- **THEN** the daemon's service-core utilization is 0% (WaitSet sleep, no polling)
