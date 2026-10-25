# logging/console-logger Specification

## Purpose

Provides an asynchronous console sink: accepted records are handed to a bounded wait-free queue by the producer thread and printed by a dedicated background writer thread, so terminal speed or tty backpressure can never stall the real-time hot path.

## Requirements

### Requirement: Asynchronous wait-free console output

`ConsoleLogger` SHALL accept records through the shared hot-path pipeline (a `nexigen::core::SpscRingBuffer` of `LogRecord` plus a background `std::jthread` writer), SHALL satisfy `concepts::Logger`, and SHALL obey the wait-free enqueue, zero-allocation, and latency guarantees of the record pipeline. The ring and the writer thread are created during initialization; the buffer capacity is fixed for the logger's lifetime.

#### Scenario: Producer never waits on the console

- **WHEN** records are logged while the background writer is busy or paused (for example a slow tty)
- **THEN** each producer call still returns immediately with OK, `BufferFull`, or (if configured) a throttle outcome, and never blocks

#### Scenario: Records reach the console in FIFO order

- **WHEN** a series of records is accepted and the logger is flushed
- **THEN** the writer emits one line per record in exactly the enqueued order, each terminated by a newline

### Requirement: Stream routing by severity

`ConsoleLogger` SHALL write records of level `Verbose`, `Debug`, and `Info` to standard output and records of level `Warning`, `Error`, and `Fatal` to standard error.

#### Scenario: Ordinary messages go to stdout

- **WHEN** an `Info` record is drained
- **THEN** its line appears on standard output and not on standard error

#### Scenario: Problems go to stderr

- **WHEN** a `Warning` (or `Error`/`Fatal`) record is drained
- **THEN** its line appears on standard error and not on standard output

### Requirement: Console line content

Each console line SHALL contain the record's timestamp, lowercase severity name, source location (file name and line), function name, and message payload, so the line is self-descriptive, and SHALL be produced without heap allocation on the writer thread.

#### Scenario: Line is self-descriptive

- **WHEN** a record from `app.cpp:42` in `drive()` with level `Info` and message `"ready"` is drained
- **THEN** the printed line includes the timestamp, `info`, `app.cpp:42`, `drive()`, and `ready`

### Requirement: Bounded single-producer contract

`ConsoleLogger` SHALL document and honor the SPSC contract: exactly one producer thread logs into a given instance, and exactly the internal worker thread consumes it; level control and snapshot queries remain callable from other threads.

#### Scenario: Contract is stated on the type

- **WHEN** the `ConsoleLogger` header is reviewed
- **THEN** its documentation states the one-producer/one-consumer requirement and the third-thread snapshot caveat, mirroring `SpscRingBuffer`'s contract

### Requirement: Flush drains asynchronously-accepted records

`flush()` on a `ConsoleLogger` SHALL request completion of writer work and wait until the ring is empty and both streams are synchronized before returning OK; `flush` is a control-plane call (teardown/checkpoint), explicitly outside the never-block hot-path guarantee that binds `log` and the convenience methods.

#### Scenario: Flush returns after the last record is visible

- **WHEN** records are enqueued and `flush()` is called
- **THEN** every previously accepted record has been written to its stream when `flush` returns

#### Scenario: Flush on an idle logger succeeds immediately

- **WHEN** `flush()` is called with an empty ring
- **THEN** it returns OK without error

#### Scenario: Destruction does not lose accepted records

- **WHEN** a `ConsoleLogger` is destroyed with records still pending
- **THEN** the worker drains what fits, joins, and no accepted-and-not-yet-written record is silently discarded without trace
