# Spec Delta

## ADDED Requirements

### Requirement: Console logger diagnostic strings

`ConsoleLogger::Config` and `ConsoleLogger` SHALL each provide a cold-path `std::string to_string() const` (plus the matching free `to_string(const T&)`): the Config dump names its initial level; the logger dump reports level, shard capacity, aggregated pending records, and drop totals as a third-thread snapshot.

#### Scenario: Config and logger render

- **WHEN** `to_string()` is called on a `ConsoleLogger::Config` at `Debug` and on its running logger
- **THEN** the Config string names `debug` and the logger string names the level, capacity, pending, and dropped counters

## REMOVED Requirements

### Requirement: Bounded single-producer contract

**Reason**: The single-producer (SPSC-per-instance) contract is superseded by per-thread SPSC ring sharding: multiple concurrent producers are now the supported topology, specified by the `logging/multi-producer` capability.
**Migration**: Implementations and callers follow `logging/multi-producer` (thread-local slot registration, per-shard wait-free writes, consumer round-robin drain); header contract text is rewritten to describe it.

## MODIFIED Requirements

### Requirement: Asynchronous wait-free console output

`ConsoleLogger` SHALL accept records through the shared hot-path pipeline (an array of per-thread `nexigen::core::SpscRingBuffer` shards of `LogRecord` plus a background `std::jthread` writer that renders deferred formats), SHALL satisfy `concepts::Logger`, and SHALL obey the wait-free enqueue, zero-allocation, and latency guarantees of the record pipeline. All shard storage and the line/scratch buffers are created during initialization; capacities are fixed for the logger's lifetime.

#### Scenario: Producer never waits on the console

- **WHEN** records are logged from several threads while the background writer is busy or paused (for example a slow tty)
- **THEN** each producer call still returns immediately (drops are counted in `dropped()`), and no thread blocks

#### Scenario: Records reach the console in FIFO order

- **WHEN** a series of records from one thread is accepted and the logger is flushed
- **THEN** the writer emits one line per record in that thread's enqueued order, each terminated by a newline

### Requirement: Console line content

Each console line SHALL contain the record's timestamp, lowercase severity name, source location (file name and line), function name, and the message rendered by the record's deferred formatting callback on the writer thread, so the line is self-descriptive, and SHALL be produced without heap allocation on the writer thread.

#### Scenario: Line is self-descriptive

- **WHEN** a record from `app.cpp:42` in `drive()` with level `Info` and call `info("ready in {} ms", 7)` is drained
- **THEN** the printed line includes the timestamp, `info`, `app.cpp:42`, `drive()`, and `ready in 7 ms`

#### Scenario: Enum and float arguments render in the line

- **WHEN** `logger.debug("Прямой вызов метода debug. Давление: {:.4f}", 0.0025);` is drained to the console at `Debug` level
- **THEN** the line's message text shows the Cyrillic sentence with `0.0025`, and formatting happened entirely on the writer thread
