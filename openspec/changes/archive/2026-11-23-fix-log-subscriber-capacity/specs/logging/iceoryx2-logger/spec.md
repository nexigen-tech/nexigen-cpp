# Spec Delta

## MODIFIED Requirements

### Requirement: Iceoryx2 logger configuration

`Iceoryx2LoggerConfig` SHALL carry the transport and worker parameters: the iceoryx2 service name, the maximum subscriber count and history depth, the worker thread attributes (core affinity), the worker idle-poll ceiling, the initial log level, and the throttle policy. The `max_subscribers` and `history_size` values SHALL equal the shared log-service constants so an application cannot require a service shape that a running daemon created with different values. Invalid values SHALL be rejected before the worker starts.

#### Scenario: Config round-trips and renders

- **WHEN** a populated `Iceoryx2LoggerConfig` passes through `to_json` then `from_json`
- **THEN** every field is recovered, `to_string()` and `std::format("{}", cfg)` name the service and the worker settings, and the formatter output equals the member output

#### Scenario: Invalid config is rejected

- **WHEN** the service name is empty or oversized, or the worker thread spec is invalid
- **THEN** initialization fails with `InvalidArgument` and no worker thread is started

#### Scenario: Default capacity matches the shared shape

- **WHEN** a default `Iceoryx2LoggerConfig` is validated
- **THEN** `max_subscribers` is 4 and `history_size` is 4

#### Scenario: Diverging capacity is rejected

- **WHEN** `max_subscribers` is set to a value other than 4, or `history_size` to a value other than 4
- **THEN** initialization returns `InvalidArgument` and no worker thread starts

## ADDED Requirements

### Requirement: Log service capacity is a shared contract

The logger SHALL create the log publish-subscribe service with the shared log-service options: at most 4 subscribers, 64 publishers, 64 nodes, a subscriber buffer depth of 16 384 samples, history depth 4, and safe overflow enabled. A publish SHALL NOT fail because a subscriber lags; the transport SHALL evict the oldest unread sample instead.

#### Scenario: Publisher installs the shared service shape

- **WHEN** an `Iceoryx2Logger` initializes on a service that does not exist yet
- **THEN** the created service reports 4 subscribers, 64 publishers, 64 nodes, a 16 384-sample subscriber depth, history 4, and safe overflow enabled

#### Scenario: A third logging application connects

- **WHEN** three applications publish log frames on the same service
- **THEN** every publisher is created successfully and all three streams reach the subscriber

#### Scenario: Slow subscriber does not fail the publisher

- **WHEN** a subscriber stops draining while the publisher sends more than 16 384 frames
- **THEN** every send completes without `ReceiveBufferFull`, the oldest unread frames are evicted, and the next received frame reports the gap through `lost_before`

#### Scenario: Existing incompatible service is rejected

- **WHEN** the logger starts while a live service was created with different capacity options
- **THEN** initialization fails with a diagnostic naming the required and the existing values
