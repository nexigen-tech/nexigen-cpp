# hw/can/bus Specification

## Purpose

Defines the vendor-neutral CAN ISO-11898 message model, per-channel identity and configuration, hardware timestamp/sequence/error provenance, bus-idle heartbeat semantics, channel state, and the `CanDriver` contract used by every CAN application.

## Requirements

### Requirement: One unified CAN message stream

The CAN layer SHALL represent every driver event as one fixed-size `CanMessage` value carrying the channel id, kind, CAN identifier, payload length, payload bytes, frame flags, error flags, origin, hardware timestamp in nanoseconds, hardware sequence number and, for bus-idle events, the idle duration. The value SHALL be trivially copyable and SHALL NOT contain owning dynamic storage.

#### Scenario: Classic data frame is represented
- **WHEN** the driver delivers an 11-bit classic data frame with eight payload bytes
- **THEN** the resulting `CanMessage` carries kind `ClassicData`, the identifier, dlc 8, the eight bytes, and the driver timestamp and sequence unchanged

#### Scenario: CAN FD data frame is represented
- **WHEN** the driver delivers a CAN FD data frame with 64 payload bytes and the BRS flag
- **THEN** the resulting `CanMessage` carries kind `FdData`, dlc 64, all 64 bytes, and the BRS flag

#### Scenario: Message remains trivially copyable
- **WHEN** `CanMessage` is copied into a bounded queue or an iceoryx2 loan
- **THEN** the copy is byte-wise and performs no allocation or destructor work

### Requirement: Every ISO-11898 frame kind is explicit

`CanMessageKind` SHALL contain `ClassicData`, `FdData`, `Remote`, `Error`, `Overload` and `BusIdle`. `Remote` SHALL be valid only for classic CAN. CAN FD remote frames, payload-bearing error/overload/bus-idle messages and classic data with dlc above 8 SHALL be rejected by validation.

#### Scenario: Remote frame is explicit
- **WHEN** the driver delivers a classic remote frame
- **THEN** the message carries kind `Remote`, the identifier, dlc 0 and no payload bytes

#### Scenario: Error and overload frames are explicit
- **WHEN** the driver reports a CAN error frame or overload frame
- **THEN** the message carries the corresponding kind and no payload bytes

#### Scenario: CAN FD remote is rejected
- **WHEN** a caller constructs a message with kind `FdData` and the remote semantic
- **THEN** validation returns `InvalidArgument` and no message is published

#### Scenario: Oversized classic payload is rejected
- **WHEN** a classic data message declares a payload length above 8
- **THEN** validation returns `InvalidArgument`

### Requirement: Hardware provenance is preserved unchanged

The library SHALL preserve the driver's hardware timestamp in nanoseconds, hardware sequence number and error flags without converting, renumbering, clearing or substituting them. The timestamp source SHALL be recorded as hardware, driver fallback or simulated. The transport sequence in the event header SHALL remain distinct from the hardware sequence.

#### Scenario: Hardware timestamp and sequence survive forwarding
- **WHEN** a driver message carries timestamp `T`, sequence `S` and error flags `E`
- **THEN** the published `CanMessage` carries exactly `T`, `S` and `E`, and the event header transport sequence is independent of `S`

#### Scenario: Timestamp source is visible
- **WHEN** a driver supplies a hardware timestamp, a fallback timestamp or a simulated timestamp
- **THEN** the message exposes the corresponding `CanTimestampSource` value

#### Scenario: Error flags are not synthesized
- **WHEN** a data frame arrives with no driver error flags
- **THEN** the published message carries zero error flags even if the channel is in a warning or passive state

### Requirement: Bus-idle heartbeats are first-class messages

The driver stream SHALL be able to carry bus-idle heartbeats as `CanMessageKind::BusIdle`. A heartbeat SHALL carry the hardware timestamp, hardware sequence when available, and `idle_duration_ns` since the last observed bus activity; it SHALL carry no CAN identifier or payload bytes.

#### Scenario: Idle bus produces a heartbeat
- **WHEN** no frame has been observed for the configured heartbeat interval
- **THEN** the driver emits a `BusIdle` message with the timestamp and the measured idle duration

#### Scenario: Heartbeat is forwarded with the frame stream
- **WHEN** the receiver app receives a `BusIdle` message
- **THEN** it publishes it on the channel `rx` service together with data and error messages

#### Scenario: Heartbeat is distinguishable from traffic
- **WHEN** a monitor or application receives a `BusIdle` message
- **THEN** the message kind distinguishes it from a data, remote, error or overload message

### Requirement: Channels have unique names and bounded configuration

Each CAN channel SHALL have a unique non-empty name of 1 to 32 characters from `[a-z0-9_-]` and a numeric channel id. A process SHALL support at most 16 channels in v1. Channel configuration SHALL include the driver channel key, bitrate, CAN FD enablement, heartbeat interval and queue capacities, and SHALL be validated before any driver channel is opened.

#### Scenario: Valid channel configuration is accepted
- **WHEN** a configuration declares `can0` with a driver key, 500 kbit/s and classic CAN
- **THEN** validation succeeds and the channel is bound to its `rx`, `tx` and `status` services

#### Scenario: Duplicate channel name is rejected
- **WHEN** two configured channels use the same name
- **THEN** validation returns `InvalidArgument` and names the duplicate

#### Scenario: Invalid channel name is rejected
- **WHEN** a channel name is empty, contains `/`, or exceeds 32 characters
- **THEN** validation returns `InvalidArgument`

#### Scenario: More than sixteen channels is rejected
- **WHEN** a process configuration declares seventeen channels
- **THEN** creation returns `InvalidArgument` before any driver channel is opened

### Requirement: Channel state and error counters are observable

The CAN layer SHALL expose `CanChannelState` (`Active`, `Warning`, `ErrorPassive`, `BusOff`, `Stopped`, `Unknown`) and a `CanChannelStatus` value carrying the current state, transmit/receive error counters, bus-off count, error-frame count, received/transmitted message counts, dropped count, sequence-gap count, last error flags and the status timestamp.

#### Scenario: Bus-off is visible
- **WHEN** the driver reports a bus-off transition
- **THEN** the channel state becomes `BusOff`, the bus-off count increments, and the next status publication carries both values

#### Scenario: Counters accumulate
- **WHEN** the driver delivers data, error and heartbeat messages and drops one message
- **THEN** the status counts distinguish received messages, error frames, heartbeats, dropped messages and sequence gaps

#### Scenario: Recovery is visible
- **WHEN** a bus-off channel recovers to active
- **THEN** the state returns to `Active` and the recovery is reflected in the next status publication

### Requirement: The driver contract is vendor-neutral

`CanDriver` SHALL expose capabilities, open-channel, submit and close operations. Capabilities SHALL report CAN FD, remote, hardware timestamp, hardware sequence, error-frame, overload-frame and heartbeat support plus the maximum channel count. The driver SHALL deliver messages through a non-allocating sink callback and SHALL NOT require a specific operating-system CAN API.

#### Scenario: Capabilities gate unsupported frames
- **WHEN** a driver reports no CAN FD support and a caller submits an FD frame
- **THEN** submission returns `UnsupportedFormat` and the frame is not delivered

#### Scenario: Driver channel open fails cleanly
- **WHEN** the driver cannot open a configured channel
- **THEN** the operation returns a failure status, no producer or consumer is created, and already opened channels are closed

#### Scenario: Submit is non-blocking
- **WHEN** the transmit queue is full
- **THEN** submission returns `BufferFull` or the driver-specific busy status without blocking the caller

#### Scenario: The contract names no operating-system API
- **WHEN** a new driver implementation is added
- **THEN** it can satisfy the contract without exposing OS-specific headers or types to `nexigen::hw::can` consumers

### Requirement: CAN vocabulary is serializable

Every public CAN enum and value type SHALL provide `to_string() const`, an ADL `to_string` overload, a `std::formatter` specialization and non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. This includes message kind, timestamp source, origin, channel state, error flags and the message/status/channel-config values.

#### Scenario: Message kind round-trips
- **WHEN** each `CanMessageKind` passes through `to_string`, `from_string`, `to_json` and `from_json`
- **THEN** the text is the canonical lowercase name and the parsed value equals the original enumerator

#### Scenario: Message kind formats directly
- **WHEN** `std::format("{}", CanMessageKind::FdData)` is evaluated
- **THEN** the result is the canonical kind name

#### Scenario: Message and status round-trip through JSON
- **WHEN** a `CanMessage` and a `CanChannelStatus` pass through `to_json` and `from_json`
- **THEN** every field is recovered, including error flags, timestamp, hardware sequence and idle duration

#### Scenario: Hostile JSON does not throw
- **WHEN** a CAN value receives non-object, wrong-typed or unknown-name JSON under `JSON_NOEXCEPTION=1`
- **THEN** parsing leaves the destination unchanged and never throws or aborts
