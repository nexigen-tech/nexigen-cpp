# logging/file-logger Specification

## Purpose

Provides the persistent sink: an asynchronous wait-free file logger that emits strict single-line JSON records with nanosecond UTC timestamps, written by a CPU-pinned background thread that owns all formatting, throttled-drop accounting, and scheduled directory rotation, so disk behavior can never hitch the producer.

## Requirements

### Requirement: Asynchronous wait-free JSON-lines file output

`FileLogger` SHALL accept records through the shared hot-path pipeline (`nexigen::core::SpscRingBuffer` of `LogRecord` plus a background writer thread), SHALL satisfy `concepts::Logger`, and SHALL append each drained record as one line of JSON to a log file under a configured directory. Ring storage, path buffers, and formatting scratch space are fully allocated at construction/initialization.

#### Scenario: Accepted records land in the file

- **WHEN** records are enqueued and the logger is flushed
- **THEN** the output contains one JSON line per accepted record, in FIFO order, each ending with a newline

#### Scenario: Producer is never exposed to disk behavior

- **WHEN** the writer thread is stalled mid-`write` or mid-rotation while producers keep logging
- **THEN** producer calls still complete in bounded time with OK/`BufferFull`/`RateLimited` and never block, spin, yield, sleep, or lock

### Requirement: Background writer CPU core affinity

`FileLogger` configuration SHALL accept a target CPU core id or core mask. The background writer thread MUST pin itself to the specified core(s) at startup via `pthread_setaffinity_np` (`cpu_set_t`), so formatting and disk I/O never preempt real-time simulation threads. Pinning failure SHALL be reported through the logger's status surface rather than ignored.

#### Scenario: Pinned writer reports the configured mask

- **WHEN** a `FileLogger` is configured with core id N and started, and the writer thread's affinity is queried with `pthread_getaffinity_np`
- **THEN** the active mask equals the configured core set

#### Scenario: Unpinned configuration is valid

- **WHEN** no core is configured
- **THEN** the logger starts and operates with default OS placement

### Requirement: Hot-path rate limiting

`FileLogger` SHALL support a configurable rate limit (maximum accepted records per time window). The limit is enforced on the hot path from lock-free state and the monotonic clock in O(1): a call exceeding the limit SHALL be dropped immediately without blocking, increment a dropped-record counter, and return `ErrorCode::RateLimited`. Ring-full drops SHALL remain reported as `ErrorCode::BufferFull`. The two drop causes SHALL be counted separately.

#### Scenario: Over-limit burst is throttled

- **WHEN** more than the configured maximum records arrive inside one window
- **THEN** the excess calls return `RateLimited` immediately, and neither the ring nor the file grows for them

#### Scenario: Within-limit traffic passes

- **WHEN** records arrive below the configured rate
- **THEN** every call returns OK and the record is enqueued

#### Scenario: Throttle adds no lock or loop

- **WHEN** the throttle check executes on an accepted call
- **THEN** it performs only relaxed atomic accesses and monotonic-time comparisons — no mutex, no spin, no syscall beyond the record's own timestamping

### Requirement: Automatic dropped-record summary

Whenever records have been dropped (by throttling or ring overflow) and logging resumes in the next allowed window, `FileLogger` SHALL automatically emit a warning-level diagnostic line into the file reporting the exact counts dropped since the previous summary, distinguishing rate-limited from buffer-full drops. The summary consumes no rate-limit allowance of the following window's user traffic and adds no measurable latency to the call that triggers it.

#### Scenario: Drops are announced on resume

- **WHEN** 1,000 records are throttled and `RateLimited`, the window resets, and a new record arrives
- **THEN** the file contains a warning diagnostic naming the exact dropped count before or adjacent to the resumed record, and the counters reset for the next window

#### Scenario: No drops means no summary

- **WHEN** logging proceeds with zero drops
- **THEN** no diagnostic lines are emitted

### Requirement: Hitch-free scheduled directory rotation

`FileLogger` SHALL support scheduled rotation — Hourly, Daily, or a custom duration. At each boundary the background writer thread alone SHALL create the next target directory (pre-allocated path buffers), open the new file, and switch the active descriptor; producer threads SHALL observe no freeze, stall, or extra latency across any rotation, and no accepted record SHALL be lost at the switch.

#### Scenario: Rotation switches at the boundary

- **WHEN** an hourly schedule crosses into the next hour while records stream continuously
- **THEN** subsequent JSON lines appear in the new period's directory/file, and lines before the boundary remain complete in the old one

#### Scenario: Producers feel nothing at rotation

- **WHEN** producers run a latency benchmark across a rotation boundary
- **THEN** their enqueue latencies show no rotation-attributable spike (bounded by the normal full/empty behavior)

#### Scenario: Zero allocation during rotation

- **WHEN** a directory is created and the descriptor switched at runtime
- **THEN** the writer thread performs no heap allocation (fixed path/scratch buffers only)

### Requirement: Strict JSON record schema

Each file line SHALL be a single JSON object with exactly this schema and field order:

`{"timestamp":"<ISO-8601 UTC, nanoseconds>","level":"<lowercase level>","app":"<configured app name>","thread_id":<int>,"message":"[<file basename>:<line>] (<function_name>) <user message>"}`

with the timestamp formatted as `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` (e.g. `2026-07-23T23:42:00.321841133Z`), generated on the writer thread into a pre-allocated buffer with no heap allocation.

#### Scenario: Field order and shape are exact

- **WHEN** a record from `src/app.cpp:42` in `void drive()` at level `Warning`, app `"hil"`, is drained
- **THEN** the line parses as JSON with keys in order `timestamp, level, app, thread_id, message`, `level` is `"warning"`, `thread_id` is numeric, and `message` starts `"[app.cpp:42] (void drive()) "` (basename only, no directory)

#### Scenario: Escaping is correct and allocation-free

- **WHEN** a message contains quotes, backslashes, or control characters
- **THEN** the emitted line is valid JSON with those characters escaped, and the writer performed no allocation

#### Scenario: Non-ASCII payloads are preserved

- **WHEN** a message contains Cyrillic UTF-8 text
- **THEN** the file line round-trips the same UTF-8 bytes inside valid JSON (no loss or corruption)

### Requirement: Nanosecond UTC timestamps via calibrated clocks

`FileLogger` SHALL calibrate the offset between `CLOCK_REALTIME` and `CLOCK_MONOTONIC_RAW` at initialization through `nexigen::chrono`'s UTC-offset calibration, and the writer thread SHALL convert each record's monotonic timestamp through that calibration to produce the UTC ISO-8601 value with nanosecond digits using `nexigen::chrono`'s time formatting, without calling `CLOCK_REALTIME` on the hot path.

#### Scenario: File timestamps track wall time

- **WHEN** a record is written shortly after a known `CLOCK_REALTIME` reading
- **THEN** its JSON timestamp differs from that wall reading only by the expected drain/format latency (sub-millisecond tolerance on an idle system)

#### Scenario: Order within file matches monotonic order

- **WHEN** consecutive records are drained
- **THEN** their formatted timestamps are non-decreasing

### Requirement: Flush synchronizes the file

`FileLogger::flush()` SHALL request the writer to drain the ring and `fsync` the active descriptor, waiting for completion before returning OK or an error status; as a control-plane call it sits outside the never-block guarantee that binds `log` and the convenience methods.

#### Scenario: Flushed data is durable

- **WHEN** records are enqueued and `flush()` returns OK
- **THEN** all previously accepted records are written to the file and synced

#### Scenario: Flush surfaces write failure

- **WHEN** the underlying file/device fails while draining after `flush()` was requested
- **THEN** `flush()` returns a failure `Status` carrying `ErrorCode::SystemError`
