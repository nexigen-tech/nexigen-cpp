# logging/console-logger Specification

## Purpose

Provides an asynchronous console sink: accepted records are handed to a bounded wait-free queue by the producer thread and printed by a dedicated background writer thread, so terminal speed or tty backpressure can never stall the real-time hot path.

## Requirements

### Requirement: Asynchronous wait-free console output

`ConsoleLogger` SHALL accept records through the shared hot-path pipeline (an array of per-thread `nexigen::core::SpscRingBuffer` shards of `LogRecord` plus a background `std::jthread` writer that renders deferred formats), SHALL satisfy `concepts::Logger`, and SHALL obey the wait-free enqueue, zero-allocation, and latency guarantees of the record pipeline. All shard storage and the line/scratch buffers are created during initialization; capacities are fixed for the logger's lifetime.

#### Scenario: Producer never waits on the console

- **WHEN** records are logged from several threads while the background writer is busy or paused (for example a slow tty)
- **THEN** each producer call still returns immediately (drops are counted in `dropped()`), and no thread blocks

#### Scenario: Records reach the console in FIFO order

- **WHEN** a series of records from one thread is accepted and the logger is flushed
- **THEN** the writer emits one line per record in that thread's enqueued order, each terminated by a newline
### Requirement: Stream routing by severity

`ConsoleLogger` SHALL write records of level `Verbose`, `Debug`, and `Info` to standard output and records of level `Warning`, `Error`, and `Fatal` to standard error.

#### Scenario: Ordinary messages go to stdout

- **WHEN** an `Info` record is drained
- **THEN** its line appears on standard output and not on standard error

#### Scenario: Problems go to stderr

- **WHEN** a `Warning` (or `Error`/`Fatal`) record is drained
- **THEN** its line appears on standard error and not on standard output

### Requirement: Console line content

Each console line SHALL contain the record's timestamp, lowercase severity name, source location (file name and line), function name, and the message rendered by the record's deferred formatting callback on the writer thread, so the line is self-descriptive, and SHALL be produced without heap allocation on the writer thread.

#### Scenario: Line is self-descriptive

- **WHEN** a record from `app.cpp:42` in `drive()` with level `Info` and call `info("ready in {} ms", 7)` is drained
- **THEN** the printed line includes the timestamp, `info`, `app.cpp:42`, `drive()`, and `ready in 7 ms`

#### Scenario: Enum and float arguments render in the line

- **WHEN** `logger.debug("Прямой вызов метода debug. Давление: {:.4f}", 0.0025);` is drained to the console at `Debug` level
- **THEN** the line's message text shows the Cyrillic sentence with `0.0025`, and formatting happened entirely on the writer thread

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

### Requirement: Console logger diagnostic strings

`ConsoleLogger::Config` and `ConsoleLogger` SHALL each provide a cold-path `std::string to_string() const` (plus the matching free `to_string(const T&)`): the Config dump names its initial level; the logger dump reports level, shard capacity, aggregated pending records, and drop totals as a third-thread snapshot.

#### Scenario: Config and logger render

- **WHEN** `to_string()` is called on a `ConsoleLogger::Config` at `Debug` and on its running logger
- **THEN** the Config string names `debug` and the logger string names the level, capacity, pending, and dropped counters

### Requirement: Runnable console logger example

The project SHALL provide an `ex_console_logger` executable that initializes a default `ConsoleLogger`, logs from multiple `std::jthread` producers through the six variadic convenience methods, flushes, prints diagnostics, and exits 0.

#### Scenario: Multi-threaded console logging completes

- **WHEN** the example starts several worker threads that each log messages at every severity and then flushes
- **THEN** initialization and flush report success without using `.value()`, every accepted message is emitted by the writer, and the process exits 0

#### Scenario: Runtime level filtering is visible

- **WHEN** the example raises the threshold with `set_level()` and then logs below and at the new threshold
- **THEN** records below the threshold produce no line, records at or above it are printed, and the logger reports the new level

#### Scenario: Levels format directly

- **WHEN** the example formats a `LogLevel` value with `std::format` and logs it as an argument
- **THEN** the output contains the canonical lowercase severity name

#### Scenario: Console diagnostics render

- **WHEN** the example prints `dropped().to_string()` and the logger's `to_string()` after flushing
- **THEN** the text reports the drop counters and the logger's level, capacity, pending count, and drop totals
