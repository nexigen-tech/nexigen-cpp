# Spec Delta

## ADDED Requirements

### Requirement: Consumer sink contract

The asynchronous logger's consumer sink SHALL satisfy `core::concepts::LifecycleSink<Sink,
Record>`, delivering each drained record through `on_event(const Record&) noexcept`, flushing
through `flush() noexcept -> core::Status`, and using `start`/`stop` for its lifecycle. The
previous duck-typed `handle`/`synchronize` names SHALL NOT remain.

#### Scenario: Sink is checked at compile time

- **WHEN** a logger is instantiated with a consumer sink
- **THEN** the sink is constrained by `core::concepts::LifecycleSink` and a missing member fails the
  build

#### Scenario: Delivery uses on_event

- **WHEN** the worker drains a record
- **THEN** it invokes `on_event(record)` and never `handle`

#### Scenario: Flush uses the shared name

- **WHEN** a flush barrier completes
- **THEN** the worker invokes `flush()` and surfaces its `core::Status`

#### Scenario: Start and stop are part of the contract

- **WHEN** a sink owns setup or teardown
- **THEN** the worker calls `start()` before the first record and `stop()` after the final drain
