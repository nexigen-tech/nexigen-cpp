# logging/log-record Specification

## Purpose

Defines the fixed-size, cache-line-aligned record that crosses the producer/consumer boundary, the thread-identity and timestamp sources that fill it within the hot-path budget, and the wait-free, allocation-free enqueue guarantee every asynchronous logger inherits.

## Requirements

### Requirement: Fixed-size trivially-copyable record

The system SHALL define `LogRecord` as a trivially copyable, cache-line-aligned (`alignas(64)`) value type carrying: a monotonic nanosecond `Timestamp`, a `LogLevel`, the producing thread's cached Linux thread id, the source location's file name, line number, and function name, and an inline fixed-size character buffer holding the message payload with its used length. The record's size SHALL be a compile-time constant with no indirection to heap memory.

#### Scenario: Record type properties hold at compile time

- **WHEN** `LogRecord` is inspected in a static assertion
- **THEN** `std::is_trivially_copyable_v`, `alignof(...) == 64`, and a fixed `sizeof` all hold

#### Scenario: Long messages truncate in place

- **WHEN** a message longer than the inline buffer capacity is logged
- **THEN** the stored payload is truncated to capacity (with a truncation indicator reserved at the tail) and the call still completes without blocking or allocating

#### Scenario: Short-circuit fields survive the handoff

- **WHEN** a record is enqueued by the producer and dequeued by the consumer
- **THEN** every field — timestamp, level, thread id, location triplet, payload bytes, length — is observed by the consumer exactly as the producer stored it

### Requirement: Cached thread identity

Each thread's Linux OS thread id (`syscall(SYS_gettid)`) SHALL be resolved once on that thread's first record, cached in a `thread_local` variable, and read from the cache for every subsequent record; the syscall SHALL never appear on the steady-state hot path.

#### Scenario: First use resolves, later uses read cache

- **WHEN** a thread logs twice
- **THEN** the syscall executes at most once for that thread, and both records carry that thread's id

#### Scenario: Distinct threads keep distinct ids

- **WHEN** a different thread logs through state that retains per-record thread ids
- **THEN** its records carry its own id, obtained on its own first use

### Requirement: Monotonic nanosecond stamping

Each accepted record SHALL carry a timestamp from `nexigen::core`'s monotonic nanosecond clock domain (CLOCK_MONOTONIC_RAW; a calibrated cycle-counter fast path is permitted where it is provably equivalent), and successive stamps observed on one producer SHALL never go backwards.

#### Scenario: Stamps are non-decreasing per producer

- **WHEN** a stream of records is produced and drained in FIFO order
- **THEN** each record's timestamp is greater than or equal to its predecessor's

#### Scenario: Stamps reflect real elapsed time

- **WHEN** a known busy-wait interval separates two records
- **THEN** their timestamp difference is within a small tolerance of the interval's true duration

### Requirement: Wait-free producer enqueue

Enqueuing a record SHALL complete in a bounded number of steps regardless of consumer state: the hot path performs no blocking call, spin-wait, yield, sleep, mutex, or CAS loop; when the ring buffer is full the record is dropped and `ErrorCode::BufferFull` is returned immediately, even with the consumer thread stalled or rotating files.

#### Scenario: Full ring never stalls the producer

- **WHEN** the ring is filled to capacity and one more record is logged
- **THEN** the call returns `BufferFull` immediately, the previously enqueued records are intact, and no thread blocked or retried in a loop

#### Scenario: Stalled consumer does not propagate

- **WHEN** the consumer thread is stopped mid-drain and the producer keeps logging until the ring fills
- **THEN** every producer call still completes in bounded time with success or `BufferFull`

### Requirement: Zero runtime allocation in record handling

The accepted-path record creation and enqueue — and the consumer-side drain of the same logger — SHALL perform no heap allocation or deallocation (no `new`, `delete`, `malloc`, `free`, or dynamic container growth) once the logger is initialized; the ring and all buffers are fully resident before the first record.

#### Scenario: Steady-state window allocates nothing

- **WHEN** a measured window of accepted and filtered log calls runs after initialization
- **THEN** the process heap-usage counters show zero change across the window

### Requirement: Hot-path latency budget

An accepted log call — level check, timestamp, thread-id read, payload copy, and `try_push` — SHALL complete in at most 20 ns median latency in a Release (`-O3 -march=native`) build on the target machine, asserted by the test suite's benchmark.

#### Scenario: Release benchmark proves the bound

- **WHEN** the latency benchmark enqueues thousands of records under Release optimization
- **THEN** the measured median per-record enqueue latency is ≤ 20 ns

#### Scenario: Budget excludes background work

- **WHEN** the benchmark window overlaps active consumer draining, JSON formatting, or file writes
- **THEN** the producer median still satisfies the budget (consumer work is never billed to the producer path)
