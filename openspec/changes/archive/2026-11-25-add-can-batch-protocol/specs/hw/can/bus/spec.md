# Spec Delta

## MODIFIED Requirements

### Requirement: The driver contract is vendor-neutral

`CanDriver` SHALL expose capabilities, open-channel, submit, batch-submit and close operations. Capabilities SHALL report CAN FD, remote, hardware timestamp, hardware sequence, error-frame, overload-frame and heartbeat support, batch-submission support and the maximum channel count. The default batch operation SHALL submit each message individually, and a batching driver MAY override it. Drivers SHALL use a non-allocating sink and name no operating-system API.

#### Scenario: Capabilities gate unsupported frames
- **WHEN** a driver reports no CAN FD support and a caller submits an FD frame
- **THEN** submission returns `UnsupportedFormat` and the frame is not delivered

#### Scenario: Driver channel open fails cleanly
- **WHEN** the driver cannot open a configured channel
- **THEN** the operation returns a failure status, no producer or consumer is created, and already opened channels are closed

#### Scenario: Submit is non-blocking
- **WHEN** the transmit queue is full
- **THEN** submission returns `BufferFull` or the driver-specific busy status without blocking the caller

#### Scenario: Batch submission falls back
- **WHEN** a driver reports no batch capability and a caller submits a batch
- **THEN** the default batch operation submits every contained message individually and reports the aggregate outcome

#### Scenario: Batch submission is non-blocking
- **WHEN** a batching driver cannot accept another batch
- **THEN** batch submission returns a busy or full status without blocking the caller

#### Scenario: The contract names no operating-system API
- **WHEN** a new driver implementation is added
- **THEN** it can satisfy the contract without exposing OS-specific headers or types to `nexigen::hw::can` consumers

## ADDED Requirements

### Requirement: CAN batches preserve message identity

`CanMessageBatch` SHALL be a fixed-size, trivially copyable envelope with the channel identity, a message count from 1 to 100, a logical `first_sequence`, and an array of `CanMessage` values. Every contained message SHALL retain its own timestamp, hardware sequence, kind, flags, origin and payload; batching SHALL NOT rewrite or synthesize those values.

#### Scenario: Batch round-trip
- **WHEN** a batch of classic, FD, remote, error, overload and bus-idle messages is submitted and received
- **THEN** every message retains its kind, channel id, identifier, payload, error flags, timestamp, hardware sequence, idle duration and origin

#### Scenario: Timestamps are not collapsed
- **WHEN** a batch contains messages with distinct hardware timestamps
- **THEN** each received message keeps its original timestamp and no timestamp is replaced by the batch publication time

#### Scenario: Batch size is bounded
- **WHEN** a caller supplies an empty batch or more than 100 messages
- **THEN** submission rejects the batch with `InvalidArgument` and no message enters the driver

#### Scenario: Logical sequence exposes lost messages
- **WHEN** a receiver observes a batch whose `first_sequence` skips values after the previous batch
- **THEN** the skipped range identifies the number of lost CAN messages, not merely one lost transport sample

### Requirement: CAN batch vocabulary is serializable

`CanMessageBatch` SHALL provide `to_string() const`, an ADL `to_string` overload, a `std::formatter` specialization and non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. `CanDriverCapabilities` SHALL expose the batch capability in the same diagnostic and JSON surfaces.

#### Scenario: Batch formats canonically
- **WHEN** a `CanMessageBatch` is formatted with `std::format`
- **THEN** the text names the channel, message count, first sequence and contained message count

#### Scenario: Batch round-trips through JSON
- **WHEN** a `CanMessageBatch` passes through `to_json` and `from_json`
- **THEN** the channel, count, first sequence and every message field are recovered

#### Scenario: Batch capabilities round-trip
- **WHEN** `CanDriverCapabilities` with batch support passes through JSON and formatting
- **THEN** the batch flag is preserved and rendered with the other capability flags

#### Scenario: Hostile batch JSON does not throw
- **WHEN** a batch receives non-object, wrong-typed, empty or oversized JSON under `JSON_NOEXCEPTION=1`
- **THEN** parsing leaves the destination unchanged and never throws or aborts
