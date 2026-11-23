# hw/can/client Specification

## Purpose

Defines the application-side `CanClient` used by `nexigen-canmon` and user applications: one process can subscribe to `rx` from several CAN channels and publish `tx` to several channels at the same time, with independent channel callbacks, bounded publishers and the project's normal lifecycle and diagnostics.

## Requirements

### Requirement: One client can mix several channels and directions

`CanClient` SHALL allow one process to declare up to sixteen channels, each with an optional `rx` subscription, an optional `tx` publisher and an optional `status` subscription. The same client SHALL be able to subscribe to one set of channels while publishing to another set, and channels SHALL remain independent.

#### Scenario: Mixed consume and produce in one process
- **WHEN** an application subscribes to `can0` and `can1` and publishes to `can1` and `can2`
- **THEN** the client delivers `can0`/`can1` receive messages and exposes separate publishers for `can1`/`can2`

#### Scenario: More than sixteen channels is rejected
- **WHEN** a client declares seventeen channels in total
- **THEN** creation returns `InvalidArgument` before any service is created

#### Scenario: Duplicate channel declaration is rejected
- **WHEN** the same channel is declared twice with incompatible settings
- **THEN** creation returns `InvalidArgument` and names the channel

### Requirement: The client lifecycle is explicit and diagnostics-gated

`CanClient::create` SHALL validate the application identity, channel declarations and service creation. `run()` SHALL start the channel workers and reject the application with `InvalidArgument` when any subscription or publisher failed to initialize. `shutdown()` SHALL be explicit and idempotent, and the destructor SHALL call it as a safety net.

#### Scenario: Failed publisher blocks run
- **WHEN** a publisher for one channel cannot be created
- **THEN** `run()` returns `InvalidArgument`, names the channel, and no callback is invoked

#### Scenario: Missing application identity fails create
- **WHEN** the client is created without an application id
- **THEN** creation returns `InvalidArgument`, prints a diagnostic naming `--app-id`, and creates no services

#### Scenario: Shutdown is idempotent
- **WHEN** `shutdown()` is called twice
- **THEN** the second call performs no work and returns success

### Requirement: Receive callbacks are serialized on the App run thread

Each `rx` subscription SHALL deliver `CanMessage` values on the `App` `run()` thread, preserving order within that channel. Callbacks SHALL accept either `(const CanMessage&)` or `(const com::EventHeader&, const CanMessage&)`. A slow callback delays subsequent callbacks for other channels, so callbacks SHALL enqueue work and return promptly.

#### Scenario: Callback receives payload and header
- **WHEN** a message arrives on a subscribed channel
- **THEN** the callback receives the full `CanMessage` and, when requested, its event header

#### Scenario: Callbacks are serialized
- **WHEN** messages for two subscribed channels are ready at the same time
- **THEN** their callbacks execute one at a time on the `App` `run()` thread

#### Scenario: Callback never runs on a transport worker
- **WHEN** the transport delivers a message
- **THEN** user code runs on the `App` `run()` thread, not on the iceoryx2 listener or driver thread

#### Scenario: Error and heartbeat callbacks are delivered
- **WHEN** the driver stream contains an error frame or a `BusIdle` heartbeat
- **THEN** the subscription delivers it with the original kind and metadata

### Requirement: Publishers preserve CAN metadata and are bounded

Each `tx` publisher SHALL accept `ClassicData`, `FdData` and `Remote` messages, stamp the application id, preserve a non-zero hardware timestamp and hardware sequence, and reject `Error`, `Overload` and `BusIdle`. Publication SHALL be bounded: a full queue SHALL return a busy status and increment a dropped or failed counter instead of blocking indefinitely.

#### Scenario: Publisher preserves a supplied timestamp
- **WHEN** an application publishes a frame with hardware timestamp `T`
- **THEN** the transmitted message keeps `T` rather than replacing it with the current time

#### Scenario: Publisher fills time for locally generated frames
- **WHEN** an application publishes a frame with a zero timestamp
- **THEN** the client supplies the current canonical time and marks it as locally generated

#### Scenario: Invalid kind is rejected
- **WHEN** an application publishes an `Error` message on `tx`
- **THEN** publication fails with `InvalidArgument` and the message is not sent

#### Scenario: Full queue is reported
- **WHEN** the publisher's bounded queue is full
- **THEN** publication returns a busy status and the failed count increases

### Requirement: Status subscriptions expose channel health

`CanClient` SHALL allow a status callback per channel. Status callbacks SHALL deliver `CanChannelStatus` on the `App` `run()` thread and SHALL be distinguishable from frame callbacks.

#### Scenario: Status is delivered separately
- **WHEN** a channel publishes a bus-off status
- **THEN** the status callback receives it even when no data frame is present

#### Scenario: Status loss is reported
- **WHEN** the status consumer reports loss
- **THEN** the client exposes the loss count without blocking frame delivery

### Requirement: Loss and diagnostics are observable

The client SHALL expose per-channel counters for received, transmitted, dropped, loss-reported and failed messages, plus the collected cold-path diagnostics. Loss callbacks SHALL run on the `App` `run()` thread and SHALL NOT allocate on the receive path.

#### Scenario: Per-channel counters are independent
- **WHEN** `can0` drops messages while `can1` is healthy
- **THEN** only the `can0` counters change

#### Scenario: Diagnostics name the failing declaration
- **WHEN** a subscription or publisher fails
- **THEN** the diagnostics contain the channel name and the failure reason

### Requirement: The client is thread-safe at the documented boundaries

`CanClient` SHALL document which operations are callable from which thread. Subscription and publisher registration SHALL happen before `run()`. Each `CanPublisher` SHALL be used from one thread at a time; separate publishers SHALL be usable concurrently. `shutdown()` SHALL be callable after `run()` returns.

#### Scenario: Registration before run
- **WHEN** an application subscribes or publishes after `run()` has started
- **THEN** the operation returns `NotInitialized` or `Conflict` and no service is created

#### Scenario: Separate publishers are concurrent
- **WHEN** two threads send through two different channel publishers
- **THEN** both publications complete without sharing mutable publisher state

### Requirement: The client exits gracefully

`CanClient` SHALL treat `SIGINT`, `SIGTERM` and `SIGHUP` as graceful stops through the existing infrastructure, drain status and telemetry, and return the recorded stop reason. Exit codes SHALL follow the existing application mapping.

#### Scenario: Signal stops every channel
- **WHEN** `SIGTERM` is delivered while the client is running
- **THEN** all channel workers stop, telemetry drains, and the process exits with code 0

#### Scenario: Stop reason is returned
- **WHEN** the client stops after `SIGHUP`
- **THEN** `run()` returns `Sighup` and the shutdown record names `sighup`
