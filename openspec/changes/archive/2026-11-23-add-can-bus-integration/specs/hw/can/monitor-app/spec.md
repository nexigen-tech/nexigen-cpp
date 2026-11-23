# Spec Delta

## Purpose

Defines `nexigen-canmon`, the real-time CAN monitor that consumes `rx` and `status` from one or more channels, renders every message kind in a readable form, supports filtering and reports loss without blocking the receive path.

## ADDED Requirements

### Requirement: The monitor subscribes to a channel set

`nexigen-canmon` SHALL accept one or more channel configurations through `--channel` and/or `--config`, and an application id through `--app-id`. It SHALL subscribe to the `rx` and `status` services of every configured channel and SHALL NOT create `tx` services. One monitor process SHALL observe several channels simultaneously.

#### Scenario: Several channels are monitored
- **WHEN** the monitor is started with `can0` and `can1`
- **THEN** it subscribes to both `rx` streams and both `status` streams

#### Scenario: Monitor does not transmit
- **WHEN** the monitor starts
- **THEN** it creates no `tx` publisher and no CAN transmission request is generated

#### Scenario: Unknown channel fails cleanly
- **WHEN** a channel name is not valid or cannot be created
- **THEN** the monitor prints a diagnostic and exits non-zero before entering the display loop

### Requirement: Every message kind is rendered distinctly

The monitor SHALL render data, remote, error, overload and bus-idle messages with their channel, hardware timestamp, message kind, direction/origin, identifier, frame flags, error flags, payload length and payload bytes. Heartbeats SHALL show their idle duration, and error frames SHALL show the error class names.

#### Scenario: Data frame row contains the essentials
- **WHEN** a classic data frame with identifier `0x123` and payload `01 02` arrives
- **THEN** the rendered row contains the channel, timestamp, kind, identifier `0x123`, dlc 2 and payload `01 02`

#### Scenario: Error frame is highlighted
- **WHEN** an error frame with bit and CRC errors arrives
- **THEN** the rendered row contains kind `Error` and the names of both error classes

#### Scenario: Heartbeat is visible as silence
- **WHEN** a `BusIdle` message arrives with an idle duration of 1.5 seconds
- **THEN** the rendered row contains kind `BusIdle` and the idle duration

#### Scenario: Origin is visible
- **WHEN** a frame marked `LocalTransmit` and a frame marked `ExternalDevice` arrive
- **THEN** their rows show different origin markers

### Requirement: Rendering is real-time and bounded

The monitor SHALL render at a configurable cadence of at most 10 ms between display updates, SHALL use a bounded display queue, and SHALL NOT block or allocate on the receive callback. When the display cannot keep up, the monitor SHALL drop the oldest pending display entries and count them.

#### Scenario: Display keeps the receive path free
- **WHEN** the display thread is slow
- **THEN** the receive callback still returns promptly and the pending queue is bounded

#### Scenario: Overload is visible
- **WHEN** display entries are dropped
- **THEN** the monitor shows or records a dropped-display count

#### Scenario: Channel order is preserved
- **WHEN** frames arrive in order on one channel
- **THEN** the monitor renders that channel's frames in the same order

### Requirement: Filters select useful traffic

The monitor SHALL support filtering by channel, CAN identifier or mask, message kind, origin and errors-only mode. Filters SHALL apply before display and SHALL be changeable at runtime without restarting subscriptions.

#### Scenario: Channel filter
- **WHEN** the channel filter is set to `can1`
- **THEN** only `can1` messages are displayed while both channels remain subscribed

#### Scenario: Identifier mask filter
- **WHEN** an identifier mask is configured
- **THEN** only messages whose identifier matches the mask are displayed

#### Scenario: Errors-only filter
- **WHEN** errors-only mode is enabled
- **THEN** data, remote and heartbeat rows are hidden and error rows remain

#### Scenario: Runtime filter change
- **WHEN** the operator changes a filter while the monitor runs
- **THEN** the new filter applies to subsequent messages without recreating services

### Requirement: Human and machine output are available

The monitor SHALL provide a human-readable line or table mode and a JSONL mode. JSONL SHALL serialize the full `CanMessage` and `CanChannelStatus` without lossy formatting. The human mode SHALL display hardware timestamps in nanoseconds and SHALL indicate the timestamp source.

#### Scenario: JSONL preserves all fields
- **WHEN** JSONL mode is selected
- **THEN** each record round-trips the message or status fields, including error flags, hardware sequence and idle duration

#### Scenario: Timestamp source is shown
- **WHEN** a message carries a hardware, fallback or simulated timestamp
- **THEN** the human output indicates the source

#### Scenario: Status is displayed
- **WHEN** a bus-off or recovered status arrives
- **THEN** the monitor displays the channel state and the relevant counters

### Requirement: The monitor exits gracefully

`nexigen-canmon` SHALL treat `SIGINT`, `SIGTERM` and `SIGHUP` as graceful stops, drain telemetry, and exit with code 0. It SHALL report transport loss and display loss before exit.

#### Scenario: SIGINT stops the monitor
- **WHEN** `SIGINT` is delivered while the monitor runs
- **THEN** the display stops, telemetry drains, and the process exits with code 0

#### Scenario: Loss is summarized
- **WHEN** the monitor exits after transport or display loss
- **THEN** the final summary includes the lost and dropped counts

### Requirement: The monitor works against virtual hardware

The monitor SHALL consume the standard `rx` and `status` services regardless of whether they were produced by hardware or by `nexigen-canrx --backend virtual`. No monitor flag or payload change SHALL be required for the virtual mode.

#### Scenario: Virtual and hardware runs are indistinguishable
- **WHEN** the same frame model is produced by the hardware or virtual backend
- **THEN** the monitor renders the same columns and values

#### Scenario: Virtual bus errors are visible
- **WHEN** `nexigen-canvbus` injects an error frame on a virtual channel
- **THEN** the monitor displays it as an error row with its flags
