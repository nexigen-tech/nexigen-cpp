# Spec Delta

## Purpose

Defines how CAN channels, messages and status cross iceoryx2: the per-channel service names, payload and event-header mapping, channel binding, queue limits, loss reporting and application identity shared by the bridge tools, `CanClient`, the monitor and user applications.

## ADDED Requirements

### Requirement: Every channel has three data-plane services

Each configured CAN channel SHALL have `nexigen/hw/can/<channel>/rx`, `nexigen/hw/can/<channel>/tx` and `nexigen/hw/can/<channel>/status` services. `<channel>` SHALL be the validated channel name. `rx` SHALL carry the full driver stream, `tx` SHALL carry transmission requests and `status` SHALL carry `CanChannelStatus` values.

#### Scenario: Services are derived from the channel name
- **WHEN** a channel named `powertrain` is configured
- **THEN** its services are `nexigen/hw/can/powertrain/rx`, `nexigen/hw/can/powertrain/tx` and `nexigen/hw/can/powertrain/status`

#### Scenario: Invalid names never reach the transport
- **WHEN** a configured channel name is empty or contains `/`
- **THEN** no service is created and the caller receives `InvalidArgument`

#### Scenario: Two channels cannot share a service
- **WHEN** two channels resolve to the same name
- **THEN** creation fails before either channel is published

### Requirement: The receive stream carries all driver event kinds

The `rx` service SHALL publish every `CanMessage` kind delivered by the driver: classic and FD data, remote, error, overload and bus-idle heartbeat. It SHALL NOT silently drop error or heartbeat messages, and consumers SHALL filter by message kind when they need only data frames.

#### Scenario: Data and error messages share the stream
- **WHEN** the driver delivers a data frame followed by an error frame on the same channel
- **THEN** both are published on `rx` in driver order with their kinds intact

#### Scenario: Heartbeats share the stream
- **WHEN** the driver delivers a bus-idle heartbeat
- **THEN** it is published on `rx` after the preceding data or error messages

#### Scenario: Adapter does not reinterpret the kind
- **WHEN** the driver publishes `Remote`, `Overload` or `BusIdle`
- **THEN** the transport carries that kind unchanged

### Requirement: The transmit stream accepts only transmittable messages

The `tx` service SHALL accept `ClassicData`, `FdData` and `Remote` messages. It SHALL reject `Error`, `Overload` and `BusIdle` messages with `InvalidArgument`, and it SHALL reject an FD message when the target channel is not configured for CAN FD.

#### Scenario: Valid transmit message is accepted
- **WHEN** a caller publishes a classic data frame on `tx`
- **THEN** the sender app receives the message and may submit it to the driver

#### Scenario: Error frame cannot be transmitted
- **WHEN** a caller publishes a message with kind `Error` on `tx`
- **THEN** the sender app rejects it and records a diagnostic or metric

#### Scenario: FD is rejected on a classic channel
- **WHEN** a caller publishes `FdData` to a channel configured as classic-only
- **THEN** the sender app rejects it with `UnsupportedFormat`

### Requirement: The event header preserves CAN provenance

Every published CAN message SHALL use the event header to carry the application identity, the hardware timestamp as the event instant when available, the hardware sequence as optional hardware metadata, the transport sequence, and the event code that distinguishes receive, transmit, error, overload and heartbeat messages. The transport sequence SHALL never replace the hardware sequence.

#### Scenario: Receive header carries identity and time
- **WHEN** the receiver publishes a frame with application id `CANRX`
- **THEN** the event header carries `CANRX`, the hardware timestamp and the corresponding event code

#### Scenario: Hardware metadata is optional but explicit
- **WHEN** a driver supplies a hardware sequence or native timestamp
- **THEN** the header marks that metadata present and preserves the value

#### Scenario: Transmit header is distinct
- **WHEN** an application publishes a transmission request
- **THEN** the header event code identifies it as a transmit request rather than a received frame

### Requirement: Channel binding is validated on both sides

Every message SHALL be bound to its channel by the service name and by the numeric channel id inside `CanMessage`. A consumer SHALL reject a message whose channel id does not match the subscribed channel's configured id, and a publisher SHALL reject a message bound to a different channel.

#### Scenario: Matching channel is accepted
- **WHEN** a message with the configured channel id arrives on that channel's `rx` service
- **THEN** the consumer delivers it to the application

#### Scenario: Mismatched channel is rejected
- **WHEN** a message with channel id 3 arrives on the service for channel id 1
- **THEN** the consumer drops it and records a diagnostic or counter

#### Scenario: Unknown channel is rejected
- **WHEN** an application publishes to a channel name it did not configure
- **THEN** publication fails with `NotFound` and no service is created

### Requirement: Queues and loss are bounded and observable

Each service SHALL use bounded queues and history, with limits supplied by the channel configuration. Overload SHALL increment a dropped counter and SHALL NOT block the receive path. Consumers SHALL receive loss information from the transport and expose it as a counter or diagnostic.

#### Scenario: Queue overflow is counted
- **WHEN** a consumer cannot keep up with the configured queue depth
- **THEN** the transport drops according to its overflow policy and the dropped count increases

#### Scenario: Loss callback is surfaced
- **WHEN** the event consumer reports lost samples
- **THEN** the application receives the loss count and records it without allocating on the receive path

#### Scenario: Configured limits are applied
- **WHEN** a channel configures queue size and history
- **THEN** the created services use those values and reject an invalid configuration before creation

### Requirement: Application identity is mandatory on the data plane

Every CAN publisher SHALL stamp a non-empty sixteen-byte application id into each message header. The id SHALL come from `--app-id` or an equivalent application configuration and SHALL be validated before any service is created.

#### Scenario: Identity is stamped
- **WHEN** a receiver or client publishes with `--app-id CANRX`
- **THEN** every message header carries `CANRX`

#### Scenario: Missing identity is rejected
- **WHEN** an application starts without an application id
- **THEN** it prints a diagnostic naming `--app-id` and exits non-zero before creating services

### Requirement: Status carries channel health

The `status` service SHALL publish `CanChannelStatus` when the channel state changes, when error counters change materially, and at a configured heartbeat interval. Consumers SHALL be able to distinguish a healthy channel, warning, error-passive, bus-off and stopped state.

#### Scenario: Bus-off is published promptly
- **WHEN** the driver reports bus-off
- **THEN** a status message with state `BusOff` is published without waiting for the periodic interval

#### Scenario: Healthy channel has a heartbeat
- **WHEN** a channel remains active without state changes
- **THEN** status is still published at the configured heartbeat interval

#### Scenario: Status is bound to the channel
- **WHEN** a status message with a mismatched channel id arrives
- **THEN** the consumer rejects it and records a diagnostic or counter
