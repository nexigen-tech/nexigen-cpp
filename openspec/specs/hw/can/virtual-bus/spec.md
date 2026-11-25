# hw/can/virtual-bus Specification

## Purpose

Defines `nexigen-canvbus`, the virtual CAN hardware and bus used for hardware-free debug runs, and `VirtualCanDriver`, which connects the unchanged `nexigen-canrx` and `nexigen-cantx` applications to it over iceoryx2 with deterministic traffic, heartbeat and fault injection.

## Requirements

### Requirement: The virtual bus is a standalone console application

`nexigen-canvbus` SHALL be a standalone executable that simulates one or more named CAN channels without physical hardware. It SHALL accept `--config` and/or repeatable `--channel`, an application id through `--app-id`, a deterministic seed, and a run duration or explicit stop. It SHALL validate the same channel configuration as hardware.

#### Scenario: Two virtual channels
- **WHEN** `nexigen-canvbus` starts with `can0` and `can1`
- **THEN** it simulates both channels independently and exposes both virtual hardware endpoints

#### Scenario: No hardware is touched
- **WHEN** the virtual bus runs on a machine with no CAN interface
- **THEN** it starts successfully and serves its virtual channels

#### Scenario: Invalid configuration fails
- **WHEN** a virtual channel name or capacity is invalid
- **THEN** the process prints a diagnostic and exits non-zero without creating services

### Requirement: Virtual hardware is exposed through the driver contract

The virtual bus SHALL exchange driver-stream messages with `VirtualCanDriver` through dedicated `nexigen/hw/can/<channel>/vbus/rx` and `nexigen/hw/can/<channel>/vbus/tx` services. Those services SHALL carry either an individual `CanMessage` or a bounded `CanMessageBatch`. `VirtualCanDriver` SHALL implement the same `CanDriver` contract as a hardware adapter, including timestamps, sequence numbers, error flags, heartbeats, origin and the optional batch-submission operation.

#### Scenario: Receiver runs unchanged
- **WHEN** `nexigen-canrx --backend virtual` connects to the virtual bus
- **THEN** it receives the simulated driver stream through `VirtualCanDriver` and publishes the standard `rx` services

#### Scenario: Sender runs unchanged
- **WHEN** `nexigen-cantx --backend virtual` submits a frame
- **THEN** `VirtualCanDriver` publishes it on `vbus/tx` and the virtual bus consumes it

#### Scenario: Application-side model is unchanged
- **WHEN** a monitor subscribes to the standard `rx` service in virtual mode
- **THEN** it sees the same `CanMessage` model as with hardware

#### Scenario: Batch endpoint is negotiated
- **WHEN** both the virtual driver and the bus support batch submission
- **THEN** `vbus/tx` and `vbus/rx` carry `CanMessageBatch` samples and the virtual driver expands them before delivering to the configured sink

#### Scenario: Legacy endpoint remains usable
- **WHEN** a peer uses the individual-message virtual-driver form
- **THEN** the bus accepts one-message batches and preserves the same delivery semantics

### Requirement: The virtual bus simulates loopback and external traffic

The virtual bus SHALL loop transmitted frames back to the receive stream after a configurable latency and jitter, SHALL mark looped frames as `LocalTransmit`, and SHALL generate externally originated frames marked `ExternalDevice`. It SHALL preserve per-channel ordering except when an explicit reorder fault is active.

#### Scenario: Transmitted frame is looped back
- **WHEN** a data frame is submitted on `vbus/tx`
- **THEN** it appears on `vbus/rx` with the same identifier and payload, origin `LocalTransmit`, and a virtual timestamp

#### Scenario: Injected external frame is distinguishable
- **WHEN** the bus injects a periodic external frame
- **THEN** the receiving application sees origin `ExternalDevice`

#### Scenario: Latency is configurable
- **WHEN** loopback latency is configured to a non-zero value
- **THEN** the looped frame's receive time reflects that delay within the configured jitter

### Requirement: The virtual bus generates bus-idle heartbeats

When a virtual channel observes no traffic for the configured heartbeat interval, the bus SHALL publish `BusIdle` heartbeats with the virtual timestamp, sequence and measured idle duration. Heartbeats SHALL stop when traffic resumes.

#### Scenario: Idle channel produces heartbeats
- **WHEN** no frame is sent or injected for the heartbeat interval
- **THEN** `BusIdle` messages appear on the virtual receive stream

#### Scenario: Traffic suppresses heartbeats
- **WHEN** a frame is looped or injected before the heartbeat interval expires
- **THEN** no idle heartbeat is produced for that interval

#### Scenario: Heartbeat duration grows
- **WHEN** silence continues across several heartbeat intervals
- **THEN** each heartbeat reports a growing idle duration

### Requirement: The virtual bus injects deterministic faults

The virtual bus SHALL support configurable fault injection for error frames, overload frames, bus-off, warning and error-passive states, dropped frames, duplicated frames, reordered frames, burst traffic and timestamp jitter. A configured seed SHALL make the fault sequence reproducible.

#### Scenario: Error frame injection
- **WHEN** bit-error injection is enabled
- **THEN** the receive stream contains an `Error` message with the corresponding error flag

#### Scenario: Bus-off injection
- **WHEN** bus-off injection fires
- **THEN** the channel publishes `BusOff` status and recovers according to the configured recovery delay

#### Scenario: Drop and duplicate faults
- **WHEN** drop or duplicate injection is enabled
- **THEN** the receive stream reflects the configured loss or repetition and the counters report it

#### Scenario: Seed reproducibility
- **WHEN** the same seed and fault scenario run twice
- **THEN** the generated message sequence is identical

### Requirement: Virtual transmissions are validated like hardware

The virtual bus SHALL reject `Error`, `Overload` and `BusIdle` messages on `vbus/tx`, SHALL reject `FdData` on a classic channel, and SHALL reject a message whose channel id does not match. Rejections SHALL be counted and SHALL NOT enter the virtual bus.

#### Scenario: Error frame cannot be transmitted
- **WHEN** a caller sends an `Error` message to `vbus/tx`
- **THEN** the virtual bus rejects it and increments the rejected count

#### Scenario: Channel mismatch is rejected
- **WHEN** a message with the wrong channel id arrives
- **THEN** the virtual bus rejects it without affecting the target channel

#### Scenario: Classic channel rejects FD
- **WHEN** an FD frame is sent to a classic-only virtual channel
- **THEN** the virtual bus returns `UnsupportedFormat`

### Requirement: The virtual bus is observable and controllable

The virtual bus SHALL expose its channel list, active faults, seed, counters and connection state through logs and status output. It SHALL support a graceful stop through `SIGINT`, `SIGTERM` and `SIGHUP`, drain telemetry, and exit with code 0.

#### Scenario: Status shows active faults
- **WHEN** fault injection is enabled
- **THEN** the status output names the active faults and the seed

#### Scenario: Graceful stop
- **WHEN** `SIGTERM` is delivered while the virtual bus runs
- **THEN** all channels stop, telemetry drains, and the process exits with code 0

#### Scenario: Counters summarize the run
- **WHEN** the virtual bus exits
- **THEN** the final summary reports transmitted, looped, injected, dropped and rejected messages per channel

### Requirement: Batched virtual transport is per-message equivalent

The virtual bus SHALL apply loopback, ordering, timestamp preservation, heartbeat accounting and fault injection to every message inside a batch. A batch SHALL preserve the order of its contained messages, and a lost batch SHALL report the lost CAN-message count derived from logical sequence and batch counts.

#### Scenario: Batch loopback preserves messages
- **WHEN** a batch with several data frames is submitted on `vbus/tx`
- **THEN** the same frames appear on `vbus/rx` in the same order with their original identifiers, payloads, timestamps, hardware sequences, kinds and origins

#### Scenario: Faults apply per message
- **WHEN** drop, duplicate, reorder, burst, error or overload injection is enabled during batch loopback
- **THEN** the fault is applied to the affected contained message and the resulting output batch is adjusted without corrupting other messages

#### Scenario: Lost batch yields a message gap
- **WHEN** a transport sample containing a batch is lost
- **THEN** the receiver reports a gap with the number of lost CAN messages derived from the next batch's `first_sequence` and the previous count

#### Scenario: Clock and sequencing stay per message
- **WHEN** a batch is looped back
- **THEN** per-message hardware timestamps and hardware sequences are preserved and the batch envelope does not substitute a single shared timestamp

### Requirement: Batch is internal to virtual driver links

The batch payload SHALL NOT change the standard `rx`, `tx` or `status` services. `nexigen-canrx` SHALL expand batches into individual `CanMessage` values before publishing standard `rx`, so `CanClient`, `nexigen-canmon`, `nexigen-canview` and existing examples require no payload migration.

#### Scenario: Standard rx remains a message stream
- **WHEN** `nexigen-canrx` receives a batch from the virtual driver
- **THEN** every contained message is published individually on the standard `rx` service in original order

#### Scenario: Standard tx remains a message stream
- **WHEN** an application publishes an ordinary `CanMessage` on `tx`
- **THEN** `nexigen-cantx` accepts it and may combine it with other requests into a batch only on the internal virtual-driver link

#### Scenario: Existing consumers compile unchanged
- **WHEN** `CanClient`, `nexigen-canmon` or `nexigen-canview` is built against the changed library
- **THEN** no source change is required for their standard `rx`/`tx` payloads
