# Spec Delta

## MODIFIED Requirements

### Requirement: Every channel has three data-plane services

Each configured CAN channel SHALL have `nexigen/hw/can/<channel>/rx`, `nexigen/hw/can/<channel>/tx` and `nexigen/hw/can/<channel>/status` services. `<channel>` SHALL be the validated channel name. `rx` and `tx` SHALL carry bounded `CanMessageBatch` packets (1..100 messages); `status` SHALL carry `CanChannelStatus` values.

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

The `rx` service SHALL publish bounded `CanMessageBatch` packets whose contained messages cover every `CanMessage` kind delivered by the driver: classic and FD data, remote, error, overload and bus-idle heartbeat. It SHALL NOT silently drop error or heartbeat messages, and consumers SHALL filter by the kind of each contained message when they need only data frames.

#### Scenario: Data and error messages share the stream
- **WHEN** the driver delivers a data frame followed by an error frame on the same channel
- **THEN** both are published on `rx` in driver order, either in one packet or consecutive packets, with their kinds intact

#### Scenario: Heartbeats share the stream
- **WHEN** the driver delivers a bus-idle heartbeat
- **THEN** it is published on `rx` after the preceding data or error messages

#### Scenario: Adapter does not reinterpret the kind
- **WHEN** the driver publishes `Remote`, `Overload` or `BusIdle`
- **THEN** the transport carries that kind unchanged

### Requirement: The transmit stream accepts only transmittable messages

The `tx` service SHALL accept `CanMessageBatch` packets whose contained messages are `ClassicData`, `FdData` or `Remote`. It SHALL reject a packet containing an `Error`, `Overload` or `BusIdle` message with `InvalidArgument`, and it SHALL reject an FD message when the target channel is not configured for CAN FD.

#### Scenario: Valid transmit message is accepted
- **WHEN** a caller publishes a packet of classic data frames on `tx`
- **THEN** the sender app receives every contained message and may submit them to the driver

#### Scenario: Error frame cannot be transmitted
- **WHEN** a packet contains a message with kind `Error`
- **THEN** the sender app rejects the packet and records a diagnostic or metric

#### Scenario: FD is rejected on a classic channel
- **WHEN** a packet contains `FdData` for a channel configured as classic-only
- **THEN** the sender app rejects the packet with `UnsupportedFormat`

### Requirement: Channel binding is validated on both sides

Every packet SHALL be bound to its channel by the service name and by the numeric `channel_id` shared by the packet and its contained messages. A consumer SHALL reject a packet whose channel id or any contained message id does not match the subscribed channel, and a publisher SHALL reject a packet bound to a different channel.

#### Scenario: Matching channel is accepted
- **WHEN** a packet with the configured channel id arrives on that channel's `rx` service
- **THEN** the consumer delivers every contained message to the application

#### Scenario: Mismatched channel is rejected
- **WHEN** a packet or a contained message has channel id 3 on the service for channel id 1
- **THEN** the consumer drops the packet and records a diagnostic or counter

#### Scenario: Unknown channel is rejected
- **WHEN** an application publishes to a channel name it did not configure
- **THEN** publication fails with `NotFound` and no service is created

## ADDED Requirements

### Requirement: Packet boundaries and ordering are explicit

The `rx` and `tx` services SHALL preserve the order of messages inside a packet and SHALL preserve packet order per channel. A packet SHALL carry its logical `first_sequence`; a consumer SHALL derive the number of missing CAN messages from the gap between consecutive packet sequences.

#### Scenario: Packet order is preserved
- **WHEN** two packets arrive on one channel
- **THEN** every message of the first packet is delivered before every message of the second packet

#### Scenario: Message order inside a packet is preserved
- **WHEN** a packet contains several messages with distinct identifiers
- **THEN** the consumer delivers them in array order

#### Scenario: Lost packet reports message count
- **WHEN** a packet's first sequence skips after the previous packet
- **THEN** the consumer reports the skipped range as lost CAN messages, not one lost transport sample

#### Scenario: Empty and oversized packets are rejected
- **WHEN** a caller publishes a packet with count 0 or above 100
- **THEN** publication is rejected with `InvalidArgument` and no message enters the service
