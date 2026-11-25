# Spec Delta

## ADDED Requirements

### Requirement: The transmit queue holds whole packets

Each channel SHALL own a bounded, single-producer/single-consumer queue whose element is a complete `CanMessageBatch`. Admitting a packet SHALL be all-or-nothing: either every contained message enters the queue in order, or none does. A packet SHALL NOT be split across queue elements, and the sender SHALL NOT merge messages from different admitted packets.

#### Scenario: Packet is admitted atomically
- **WHEN** a 100-message packet with a matching channel id arrives on `tx`
- **THEN** the sender admits the packet as one queue element and reports 100 accepted messages

#### Scenario: Message order is preserved
- **WHEN** a packet contains messages with distinct identifiers
- **THEN** the queue and the driver receive those messages in packet array order

#### Scenario: Full queue drops the whole packet
- **WHEN** the channel queue has no free element and a 100-message packet arrives
- **THEN** the sender rejects the packet, drops all 100 messages, and increments the dropped count by 100

#### Scenario: Invalid message rejects the whole packet
- **WHEN** any contained message has a mismatched channel id, an untransmittable kind, or an FD payload on a classic-only channel
- **THEN** the sender rejects the entire packet, submits none of its messages, and records the rejection

#### Scenario: Empty or oversized packet is rejected
- **WHEN** a packet has count 0 or count above the batch maximum
- **THEN** the sender rejects it with `InvalidArgument` and changes no counter except the rejected count

#### Scenario: Single-message enqueue still works
- **WHEN** an application calls the single-message enqueue path
- **THEN** the message is wrapped as a one-message packet and follows the same admission and submission rules

### Requirement: The submit path does not re-batch admitted packets

For every dequeued packet, the sender SHALL submit the packet to the driver as one batch operation, preserving the packet's message count and order. It SHALL NOT unpack, re-validate or rebuild the admitted packet before handing it to the driver; the driver's own batch contract remains the only validation on that path.

#### Scenario: One driver batch per dequeued packet
- **WHEN** the worker dequeues a 100-message packet
- **THEN** the driver receives one batch submission with count 100

#### Scenario: Driver backpressure is counted per message
- **WHEN** the driver rejects a dequeued packet with a busy or error status
- **THEN** the failed count increases by the packet's message count and no message is reported as submitted

#### Scenario: Packet metadata reaches the driver unchanged
- **WHEN** the worker dequeues a packet with non-zero timestamps, hardware sequences and mixed data kinds
- **THEN** the driver batch carries the same timestamps, sequences, kinds and payloads

### Requirement: Transmitter diagnostics stay renderable

`CanTransmitter` SHALL keep a cold-path `to_string()` representation and a `std::formatter` specialization. The representation SHALL report the configured channel count and the number of successfully submitted messages.

#### Scenario: Diagnostic text is stable
- **WHEN** a transmitter with two channels has submitted 250 messages
- **THEN** `to_string()` contains the channel count 2 and the submitted count 250

#### Scenario: Formatting matches the diagnostic
- **WHEN** a `CanTransmitter` is formatted with `std::format`
- **THEN** the output equals `to_string()`
