# Spec Delta

## Purpose

Defines `nexigen-cantx`, the driver-side bridge that subscribes to CAN transmission requests on iceoryx2 and submits them through the configured CAN driver while preserving timestamps and reporting driver backpressure and errors.

## ADDED Requirements

### Requirement: The sender accepts a channel list

`nexigen-cantx` SHALL accept one or more channel configurations through `--channel` and/or `--config`, an application id through `--app-id`, and a backend selector `hardware` or `virtual`. It SHALL support one process per channel and one process for all configured channels with the same command shape.

#### Scenario: Hardware adapter is required
- **WHEN** `--backend hardware` is selected and no vendor adapter is linked
- **THEN** the process prints a diagnostic naming the adapter and exits non-zero before creating services

#### Scenario: One channel per process
- **WHEN** `nexigen-cantx --channel can0 --app-id CANTX` starts
- **THEN** it subscribes only to `nexigen/hw/can/can0/tx`

#### Scenario: One process for all channels
- **WHEN** the same process is configured with `can0` and `can1`
- **THEN** it subscribes to both tx services and maintains independent transmit queues

#### Scenario: Invalid channel fails before subscribing
- **WHEN** a configured channel name is invalid
- **THEN** the process prints a diagnostic, creates no subscription, and exits non-zero

### Requirement: Only transmittable messages reach the driver

The sender SHALL accept `ClassicData`, `FdData` and `Remote` messages whose channel id matches the subscribed channel. It SHALL reject `Error`, `Overload` and `BusIdle`, and SHALL reject `FdData` on a classic-only channel. Rejection SHALL increment a counter and produce a diagnostic without terminating the process.

#### Scenario: Valid data frame is submitted
- **WHEN** a matching classic data message arrives on `tx`
- **THEN** the sender submits it to the driver with its payload, identifier and timestamp unchanged

#### Scenario: Error frame is ignored
- **WHEN** an `Error` message arrives on `tx`
- **THEN** the sender rejects it, increments the rejected count, and continues running

#### Scenario: Mismatched channel is rejected
- **WHEN** a message with channel id 2 arrives on the service for channel id 1
- **THEN** the sender rejects it and records the channel mismatch

### Requirement: Transmission is bounded and non-blocking

Each channel SHALL own a bounded transmit queue. The sender SHALL submit without blocking the iceoryx2 callback, SHALL retry a driver-busy result within a bounded deadline, and SHALL report `BufferFull` or the driver status when the deadline expires. A driver-busy or full queue SHALL NOT stall other channels.

#### Scenario: Driver backpressure is reported
- **WHEN** the driver returns a busy status for a submission
- **THEN** the sender retries within the configured deadline and otherwise increments the failed count

#### Scenario: One channel does not block another
- **WHEN** `can0` is blocked in driver backpressure
- **THEN** `can1` continues to accept and submit messages

#### Scenario: Full queue is counted
- **WHEN** a channel transmit queue is full
- **THEN** the new message is dropped or rejected according to policy and the dropped count increases

### Requirement: Driver errors are surfaced without hiding state

The sender SHALL record driver submission failures, bus-off transitions, warning and error-passive states, and transmit error counters. It SHALL publish channel status when the driver reports a state change and SHALL NOT claim a message was transmitted when the driver rejected it.

#### Scenario: Bus-off stops confident transmission
- **WHEN** the driver reports bus-off
- **THEN** the sender publishes `BusOff`, stops claiming successful transmissions, and follows the configured recovery policy

#### Scenario: Rejected submission is visible
- **WHEN** the driver rejects a frame
- **THEN** the failed count increases and the diagnostic names the channel and driver status

#### Scenario: Recovery is reported
- **WHEN** the driver returns to active after bus-off
- **THEN** the sender publishes `Active` and resumes accepting messages according to policy

### Requirement: Transmitted messages preserve application metadata

For every accepted request, the sender SHALL preserve the message kind, CAN identifier, payload, frame flags, error flags, origin and a non-zero hardware timestamp. For a request with zero timestamp, the sender SHALL use the current canonical time and mark the message as locally generated. The sender SHALL NOT rewrite the channel id or application id.

#### Scenario: Supplied timestamp is preserved
- **WHEN** a request carries a non-zero hardware timestamp
- **THEN** the submitted driver message carries the same timestamp

#### Scenario: Zero timestamp is filled locally
- **WHEN** a request carries a zero timestamp
- **THEN** the sender supplies the current canonical time and marks the origin as locally generated

#### Scenario: Origin is explicit
- **WHEN** the sender submits a locally requested frame
- **THEN** the driver message is marked `LocalTransmit`

### Requirement: Startup, shutdown and telemetry are explicit

The sender SHALL initialize the time base and telemetry through the project's infrastructure, create every subscription, and only then enter the transmit loop. `SIGINT`, `SIGTERM` and `SIGHUP` SHALL drain telemetry and exit with code 0. The sender SHALL expose per-channel accepted, transmitted, rejected, dropped and failed counters and identify them with the application id.

#### Scenario: Subscription failure rolls back
- **WHEN** the second channel subscription cannot be created
- **THEN** the first subscription is released, no transmit loop starts, and the process exits non-zero

#### Scenario: SIGTERM drains
- **WHEN** `SIGTERM` is delivered while the sender is running
- **THEN** every channel stops, telemetry drains, and the process exits with code 0

#### Scenario: Counters are attributable
- **WHEN** the sender processes two channels
- **THEN** every metric and log record identifies the channel and the application id

### Requirement: Virtual backend is behaviorally identical

With `--backend virtual`, `nexigen-cantx` SHALL use `VirtualCanDriver` and publish requests to `nexigen-canvbus` over the virtual hardware services. The application-side `tx` subscription, validation and counters SHALL remain identical to the hardware backend.

#### Scenario: Virtual transmission reaches the virtual bus
- **WHEN** a message is published on `nexigen/hw/can/can0/tx` and `nexigen-cantx --backend virtual` is running
- **THEN** `nexigen-canvbus` receives it on its virtual hardware input

#### Scenario: Monitor sees the loopback
- **WHEN** the virtual bus loops the transmitted frame back
- **THEN** `nexigen-canrx --backend virtual` publishes it on `nexigen/hw/can/can0/rx` with origin `LocalTransmit`
