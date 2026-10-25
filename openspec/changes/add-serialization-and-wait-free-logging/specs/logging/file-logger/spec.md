# Spec Delta

## ADDED Requirements

### Requirement: Rotation schedule vocabulary

`Rotation_schedule` (`detail::Rotation_kind`) SHALL provide, in its namespace: `[[nodiscard]] constexpr std::string_view to_string(Rotation_schedule) noexcept` naming each enumerator (`"none"`, `"hourly"`, `"daily"`, `"custom"`); `from_string` accepting those spellings; ADL `to_json`/`from_json` (from_json non-throwing under `JSON_NOEXCEPTION=1`, leaving the target unchanged on invalid input); and a `std::formatter` specialization delegating to `std::string_view`.

#### Scenario: Schedule round-trips through JSON

- **WHEN** each `Rotation_schedule` enumerator passes through `to_json` then `from_json`
- **THEN** the JSON is the lowercase name string and the recovered value is the original enumerator

#### Scenario: Invalid schedule JSON is rejected silently

- **WHEN** `from_json` reads `{"kind": 7}` or `{"kind": "weekly"}` into a `Rotation_schedule` pre-set to `Hourly`
- **THEN** the call returns normally and the value remains `Hourly`

#### Scenario: Schedule formats directly

- **WHEN** `std::format("rotate={}", Rotation_schedule::Daily)` is evaluated
- **THEN** it yields `"rotate=daily"`

### Requirement: File logger vocabulary diagnostic strings

`Throttle_config`, `Rotation_config`, `FileLogger_config`, and `Dropped_stats` SHALL each provide a cold-path `std::string to_string() const` (plus matching free `to_string(const T&)`) rendering every field (enabled flag, budgets and windows, schedule and custom period, app/base_dir/file_name/level/core_mask, per-cause drop totals) for configuration dumps and test evidence.

#### Scenario: Config dump is complete

- **WHEN** `to_string()` is called on a populated `FileLogger_config`
- **THEN** the returned string contains the app name, base directory, file name, level name, throttle window, and rotation schedule name

#### Scenario: Drop snapshot renders

- **WHEN** `to_string()` is called on a `Dropped_stats` with nonzero counters
- **THEN** the string names each cause and count

## MODIFIED Requirements

### Requirement: Asynchronous wait-free JSON-lines file output

`FileLogger` SHALL accept records through the shared hot-path pipeline (an array of per-thread `SpscRingBuffer` shards of `LogRecord` plus a background writer thread that renders deferred formats), SHALL satisfy `concepts::Logger`, and SHALL append each drained record as one line of JSON to a log file under a configured directory. Ring storage, path buffers, and formatting scratch space are fully allocated at construction/initialization.

#### Scenario: Accepted records land in the file

- **WHEN** records enqueued from one or more threads are flushed
- **THEN** the output contains one JSON line per accepted record, each ending with a newline, ordered FIFO within each producing thread

#### Scenario: Producer is never exposed to disk behavior

- **WHEN** the writer thread is stalled mid-`write` or mid-rotation while producers keep logging
- **THEN** producer calls still complete in bounded time (drops counted, not returned) and never block, spin, yield, sleep, or lock

### Requirement: Hot-path rate limiting

`FileLogger` SHALL support a configurable rate limit (maximum accepted records per time window), enforced per producer shard from lock-free state and the monotonic clock in O(1). A call exceeding the limit SHALL be dropped immediately without blocking and SHALL increment that shard's rate-limited drop counter; ring-full drops SHALL keep their own buffer-full counter. The two drop causes SHALL be counted separately and exposed through `dropped()`.

#### Scenario: Over-limit burst is throttled

- **WHEN** more than the configured maximum records arrive from one thread inside one window
- **THEN** the excess calls return immediately having enqueued nothing, the rate-limited counter grows, and neither the ring nor the file grows for them

#### Scenario: Within-limit traffic passes

- **WHEN** records arrive below the configured rate
- **THEN** every call is accepted and enqueued

#### Scenario: Throttle adds no lock or loop

- **WHEN** the throttle check executes on an accepted call
- **THEN** it performs only shard-local reads and monotonic-time comparisons — no mutex, no spin, no shared atomic RMW, no syscall beyond the record's own timestamping

### Requirement: Automatic dropped-record summary

Whenever records have been dropped (by throttling or ring overflow) and logging resumes in the next allowed window, `FileLogger` SHALL automatically emit a warning-level diagnostic line into the file reporting the exact counts dropped since the previous summary, distinguishing rate-limited from buffer-full drops. The summary consumes no rate-limit allowance of the following window's user traffic and adds no measurable latency to the call that triggers it.

#### Scenario: Drops are announced on resume

- **WHEN** 1,000 records from a thread exceed the throttle window, the window resets, and a new record arrives
- **THEN** the file contains a warning diagnostic naming the exact dropped count before or adjacent to the resumed record, and the counters reset for the next window

#### Scenario: No drops means no summary

- **WHEN** logging proceeds with zero drops
- **THEN** no diagnostic lines are emitted

### Requirement: Strict JSON record schema

Each file line SHALL be a single JSON object with exactly this schema and field order:

`{"timestamp":"<ISO-8601 UTC, nanoseconds>","level":"<lowercase level>","app":"<configured app name>","thread_id":<int>,"message":"[<file basename>:<line>] (<function_name>) <rendered message>"}`

with the timestamp formatted as `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ`, the `<rendered message>` produced by invoking the record's deferred formatting callback on the writer thread into a pre-allocated buffer, all generated without heap allocation.

#### Scenario: Field order and shape are exact

- **WHEN** a record from `src/app.cpp:42` in `void drive()` at level `Warning`, app `"hil"`, call `warning("bus {}", 3)` is drained
- **THEN** the line parses as JSON with keys in order `timestamp, level, app, thread_id, message`, `level` is `"warning"`, `thread_id` is numeric, and `message` starts `"[app.cpp:42] (void drive()) bus 3"` (basename only, no directory)

#### Scenario: Escaping is correct and allocation-free

- **WHEN** a rendered message contains quotes, backslashes, or control characters
- **THEN** the emitted line is valid JSON with those characters escaped, and the writer performed no allocation

#### Scenario: Non-ASCII payloads are preserved

- **WHEN** a format string or argument contains Cyrillic UTF-8 text
- **THEN** the file line round-trips the same UTF-8 bytes inside valid JSON (no loss or corruption)
