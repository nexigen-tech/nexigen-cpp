# Spec Delta

## ADDED Requirements

### Requirement: Runnable SPSC telemetry example

The project SHALL provide an `ex_spsc_ring_buffer` executable that demonstrates a real-time producer/consumer telemetry pipeline using a statically stored bounded SPSC queue of capacity 1024 whose element is a trivially copyable packet carrying a monotonic timestamp, a sequence number, and a sensor value.

#### Scenario: Telemetry packets cross threads intact

- **WHEN** the example runs its single producer and single consumer over a sequence of telemetry packets
- **THEN** every pushed packet is received exactly once in sequence order with its timestamp and sensor value unchanged, and the process exits 0

#### Scenario: Packet diagnostic representation

- **WHEN** the example renders a telemetry packet through `to_string()`
- **THEN** the text identifies its timestamp, sequence number, and sensor value

### Requirement: Wait-free outcome handling in the example

The example SHALL handle queue outcomes by inspecting `has_value()` and reading `.error()` on failure, SHALL distinguish `BufferFull` from `BufferEmpty`, and SHALL NOT call `.value()` anywhere.

#### Scenario: Full queue is classified

- **WHEN** the producer attempts a push while the queue has no free slot
- **THEN** the example observes the `BufferFull` error via `.error()` and continues without blocking, panicking, or discarding an accepted packet

#### Scenario: Empty queue is classified

- **WHEN** the consumer attempts a pop before any packet is available
- **THEN** the example observes the `BufferEmpty` error via `.error()` and continues without blocking or aborting

### Requirement: Example diagnostic formatting

The example SHALL format `ErrorCode` values directly with `std::format` and SHALL print a cold-path queue summary through `to_string()` after the pipeline completes.

#### Scenario: Error codes render directly

- **WHEN** the example formats an observed `BufferFull` or `BufferEmpty` value
- **THEN** the output contains the canonical error spelling without manual conversion

#### Scenario: Final queue summary

- **WHEN** the producer and consumer have finished and the queue is drained
- **THEN** the printed `to_string()` summary reports capacity 1024, zero stored elements, and the empty state
