# logging/log-record Specification

## Purpose

Defines the fixed-size, cache-line-aligned record that crosses the producer/consumer boundary, the thread-identity and timestamp sources that fill it within the hot-path budget, and the wait-free, allocation-free enqueue guarantee every asynchronous logger inherits.

## Requirements

### Requirement: Fixed-size trivially-copyable record

The system SHALL define `LogRecord` as a trivially copyable, cache-line-aligned (`alignas(64)`) fixed 256-byte value type carrying: a monotonic nanosecond `Timestamp`, a `LogLevel`, the producing thread's cached Linux thread id, the source location's file/line/function, the format-string pointer, the type-erased formatting callback pointer, and an inline fixed-size `arg_payload_` blob with its used length. `std::string_view`, `const char*`, and `std::span<const char>` arguments are `memcpy`-copied into `arg_payload_` at pack time (safe truncation with a flag) so the consumer never reads a destroyed producer stack. The record's size is a compile-time constant with no indirection to heap memory.

#### Scenario: Record type properties hold at compile time

- **WHEN** `LogRecord` is inspected in a static assertion
- **THEN** `std::is_trivially_copyable_v`, `alignof(...) == 64`, `sizeof(...) == 256`, and a fixed `sizeof` all hold

#### Scenario: String-like arguments are copied into record-owned storage

- **WHEN** a `std::string_view` or `const char*` argument references a stack buffer that is destroyed before the record is drained
- **THEN** the consumer observes the copied bytes intact (truncated to `arg_payload_` capacity with the truncation flag set when too long), never a dangling pointer, and ASan reports no use-after-free

#### Scenario: Long messages truncate in place

- **WHEN** a packed string-like argument exceeds the inline `arg_payload_` allowance
- **THEN** the stored copy is truncated to capacity with the truncation flag set, the call completes without blocking or allocating, and the consumer renders the truncated text

#### Scenario: Short-circuit fields survive the handoff

- **WHEN** a record is enqueued by the producer and dequeued by the consumer
- **THEN** every field — timestamp, level, thread id, location triplet, format pointer, callback pointer, payload bytes, length — is observed by the consumer exactly as the producer stored it
### Requirement: Cached thread identity

Each thread's Linux OS thread id (`syscall(SYS_gettid)`) SHALL be resolved once on that thread's first record, cached in a `thread_local` variable, and read from the cache for every subsequent record; the syscall SHALL never appear on the steady-state hot path.

#### Scenario: First use resolves, later uses read cache

- **WHEN** a thread logs twice
- **THEN** the syscall executes at most once for that thread, and both records carry that thread's id

#### Scenario: Distinct threads keep distinct ids

- **WHEN** a different thread logs through state that retains per-record thread ids
- **THEN** its records carry its own id, obtained on its own first use

### Requirement: Monotonic nanosecond stamping

Each accepted record SHALL carry a timestamp from `nexigen::chrono`'s monotonic nanosecond clock domain (CLOCK_MONOTONIC_RAW; a calibrated cycle-counter fast path is permitted where it is provably equivalent), and successive stamps observed on one producer SHALL never go backwards.

#### Scenario: Stamps are non-decreasing per producer

- **WHEN** a stream of records is produced and drained in FIFO order
- **THEN** each record's timestamp is greater than or equal to its predecessor's

#### Scenario: Stamps reflect real elapsed time

- **WHEN** a known busy-wait interval separates two records
- **THEN** their timestamp difference is within a small tolerance of the interval's true duration

### Requirement: Wait-free producer enqueue

Enqueuing a record into the calling thread's own shard SHALL complete in a bounded number of steps regardless of consumer state: the hot path performs no blocking call, spin-wait, yield, sleep, mutex, or CAS loop; when the shard ring is full the record is dropped and counted in the buffer-full total, and the call returns immediately, even with the consumer thread stalled or rotating files.

#### Scenario: Full ring never stalls the producer

- **WHEN** the calling thread's shard is filled to capacity and one more record is logged
- **THEN** the call returns immediately, the previously enqueued records are intact, the buffer-full drop total increases, and no thread blocked or retried in a loop

#### Scenario: Stalled consumer does not propagate

- **WHEN** the consumer thread is stopped mid-drain and producers keep logging until their shards fill
- **THEN** every producer call still completes in bounded time, accepted or dropped-and-counted
### Requirement: Zero runtime allocation in record handling

The accepted-path record creation and enqueue — and the consumer-side drain of the same logger — SHALL perform no heap allocation or deallocation (no `new`, `delete`, `malloc`, `free`, or dynamic container growth) once the logger is initialized; the ring and all buffers are fully resident before the first record.

#### Scenario: Steady-state window allocates nothing

- **WHEN** a measured window of accepted and filtered log calls runs after initialization
- **THEN** the process heap-usage counters show zero change across the window

### Requirement: Hot-path latency budget

An accepted log call — level check, timestamp, thread-id read, shard routing, argument packing into the record, and `try_push` — SHALL complete in at most 20 ns median latency in a Release (`-O3 -march=native`) build on the target machine, asserted by the test suite's benchmark.

#### Scenario: Release benchmark proves the bound

- **WHEN** the latency benchmark enqueues thousands of records under Release optimization
- **THEN** the measured median per-record enqueue latency is ≤ 20 ns

#### Scenario: Budget excludes background work

- **WHEN** the benchmark window overlaps active consumer draining, deferred formatting, JSON rendering, or file writes
- **THEN** the producer median still satisfies the budget (consumer work is never billed to the producer path)

### Requirement: Deferred type-erased argument formatting

An accepted variadic log call SHALL pack into the record the raw format-string pointer, a compile-time-generated type-erased formatting callback specialized for the argument pack, and the serialized arguments themselves (trivially copyable scalars, enums, and bounded inline copies of string-like arguments). The producer thread SHALL NOT execute `std::format_to_n` or any float-to-string conversion.

#### Scenario: Producer packs without rendering

- **WHEN** `logger.info("value {} {}", 3.5, LogLevel::Info)` is accepted in a measured Release window
- **THEN** the producer thread performs no `std::format` machinery (argument bytes plus callback pointer land in the record) and the steady-state allocation counter shows zero growth

### Requirement: Consumer-side rendering of packed records

The consumer thread SHALL invoke the packed callback to render the final message into pre-allocated scratch buffers when writing console lines or JSON records, producing text identical to formatting at the call site, with no heap allocation on either side.

#### Scenario: Consumer renders exactly as the call site would

- **WHEN** the record from `info("value {} {}", 3.5, LogLevel::Info)` is drained to a sink
- **THEN** the rendered message reads `value 3.5 info`

#### Scenario: Render overflow is bounded

- **WHEN** a packed argument set renders longer than the sink scratch capacity
- **THEN** the consumer truncates the rendered text (flagged, never half-emitted JSON) and continues draining without stalling

### Requirement: LogRecord diagnostic string

`LogRecord` SHALL provide a cold-path `std::string to_string() const` (member, plus free `to_string(const LogRecord&)`) rendering timestamp, level, thread id, location triplet, and the message or (before rendering) the format-string text and argument summary — for post-mortem inspection on the consumer or in tests, never on a hot path.

#### Scenario: Consumer-side dump is complete

- **WHEN** `to_string()` is called on a drained record
- **THEN** the returned string contains the level name, the file/line/function, and the payload representation
