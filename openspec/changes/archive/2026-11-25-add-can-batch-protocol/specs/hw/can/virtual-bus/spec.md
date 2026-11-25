# Spec Delta

## MODIFIED Requirements

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

## ADDED Requirements

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
