# hw/can/receiver-app Specification

## Purpose

Defines `nexigen-canrx`, the driver-side bridge that receives the complete CAN driver stream and publishes it to iceoryx2 without changing timestamps, sequence numbers, error flags, message kinds or bus-idle heartbeats.

## Requirements

### Requirement: The receiver accepts a channel list

`nexigen-canrx` SHALL accept one or more channel configurations through `--channel` and/or `--config`, an application id through `--app-id`, and a backend selector `hardware` or `virtual`. It SHALL support one process per channel and one process for all configured channels with the same command shape.

#### Scenario: Hardware adapter is required
- **WHEN** `--backend hardware` is selected and no vendor adapter is linked
- **THEN** the process prints a diagnostic naming the adapter and exits non-zero before creating services

#### Scenario: One channel per process
- **WHEN** `nexigen-canrx --channel can0 --app-id CANRX` starts
- **THEN** it opens only `can0` and publishes only `can0` services

#### Scenario: One process for all channels
- **WHEN** the same process is configured with `can0` and `can1`
- **THEN** it opens both channels and publishes both channel streams independently

#### Scenario: Invalid channel fails before opening hardware
- **WHEN** a configured channel name or driver key is invalid
- **THEN** the process prints a diagnostic, creates no service, and exits non-zero

### Requirement: The receiver preserves the driver stream

For every driver message, `nexigen-canrx` SHALL publish the same kind, channel id, CAN identifier, payload, frame flags, error flags, origin, hardware timestamp, hardware sequence and idle duration. It SHALL NOT convert, renumber, clear, reorder within a channel, or synthesize metadata.

#### Scenario: Data frame is forwarded unchanged
- **WHEN** the driver delivers a classic or FD data frame
- **THEN** the published message matches the driver message field by field

#### Scenario: Error frame is forwarded
- **WHEN** the driver delivers an error frame with error flags
- **THEN** the published message carries kind `Error` and the same flags

#### Scenario: Heartbeat is forwarded
- **WHEN** the driver delivers a `BusIdle` heartbeat with an idle duration
- **THEN** the published message carries kind `BusIdle` and the same timestamp and duration

#### Scenario: Origin is preserved
- **WHEN** the driver marks a message as locally transmitted or externally generated
- **THEN** the published message carries the same origin

### Requirement: Per-channel rings isolate overload

Each channel SHALL own a bounded SPSC ring between the driver callback and the publishing thread. A full ring SHALL drop according to the configured policy, increment a dropped counter and SHALL NOT block the driver callback or another channel.

#### Scenario: Full ring does not block the driver
- **WHEN** the publishing thread stalls and a channel ring fills
- **THEN** the driver callback returns without blocking and the dropped count increases

#### Scenario: Channels do not interfere
- **WHEN** `can0` is overloaded and drops messages
- **THEN** `can1` continues to publish without dropped messages

#### Scenario: Drop policy is observable
- **WHEN** the receiver stops or restarts
- **THEN** the per-channel counters report received, published and dropped messages

### Requirement: Status reflects channel health

`nexigen-canrx` SHALL publish `CanChannelStatus` on the channel status service when the channel state changes, when error counters change, and at the configured status heartbeat. It SHALL derive status from driver-reported state and counters, never from guessed protocol state.

#### Scenario: Bus-off is published immediately
- **WHEN** the driver reports bus-off
- **THEN** a `BusOff` status is published before the next periodic status

#### Scenario: Healthy channel is reported
- **WHEN** a channel remains active without errors
- **THEN** periodic `Active` status is still published

#### Scenario: Driver state is not invented
- **WHEN** the driver does not report a state transition
- **THEN** the receiver does not synthesize warning, passive or bus-off states

### Requirement: Startup and shutdown are explicit

The receiver SHALL initialize the time base and telemetry through the project's infrastructure, open every configured driver channel, and only then enter the receive loop. `SIGINT`, `SIGTERM` and `SIGHUP` SHALL be graceful stops that drain telemetry and exit with code 0. A failed driver open SHALL roll back already opened channels and services.

#### Scenario: Driver open failure rolls back
- **WHEN** the second configured channel cannot be opened
- **THEN** the first channel is closed, no publisher remains, and the process exits non-zero with the driver status

#### Scenario: SIGHUP drains and exits cleanly
- **WHEN** `SIGHUP` is delivered while the receiver is running
- **THEN** the receiver stops, drains telemetry, and exits with code 0

#### Scenario: Repeated shutdown is safe
- **WHEN** shutdown runs explicitly and again from the destructor
- **THEN** the second path performs no work

### Requirement: The receiver exposes operational telemetry

The receiver SHALL record per-channel received, published and dropped counters, driver error counters, ring high-water marks, and startup/stop diagnostics through the existing metrics, logging and tracing facilities. The application id SHALL identify all telemetry and CAN messages.

#### Scenario: Counters are attributable
- **WHEN** the receiver records messages for two channels
- **THEN** every metric and log record identifies the channel and the application id

#### Scenario: Startup diagnostics name failures
- **WHEN** the time base, a service or a driver channel fails
- **THEN** one diagnostic names the stage and the failure before the process exits

### Requirement: Virtual backend is behaviorally identical

With `--backend virtual`, `nexigen-canrx` SHALL use `VirtualCanDriver` and produce the same `rx` and `status` streams as the hardware backend. Switching backends SHALL NOT change the application-side service names or payloads.

#### Scenario: Virtual run feeds the same topics
- **WHEN** `nexigen-canrx --backend virtual --channel can0` runs against `nexigen-canvbus`
- **THEN** a subscriber to `nexigen/hw/can/can0/rx` receives the simulated frames with preserved timestamps and kinds

#### Scenario: Monitor works unchanged
- **WHEN** `nexigen-canmon` subscribes to the virtual receiver's `rx` service
- **THEN** it renders the same message model as with hardware
