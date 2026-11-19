# Spec Delta

## MODIFIED Requirements

### Requirement: Strict JSON record schema

Each file line SHALL be a single JSON object with exactly this schema and field order:

`{"timestamp":"<ISO-8601 UTC, nanoseconds>","mono_ns":<canonical nanoseconds>,"boot":"<boot token>","level":"<lowercase level>","app":"<configured app name>","thread_id":<int>,"message":"[<file basename>:<line>] (<function_name>) <rendered message>"}`

timestamp format `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ`; `mono_ns` is the record's canonical `CLOCK_MONOTONIC_RAW` instant; `boot` is the boot token; `<rendered message>` produced by the record's deferred formatting callback on the writer thread into a pre-allocated buffer, without heap allocation.

#### Scenario: Field order and shape are exact

- **WHEN** a record from `src/app.cpp:42` in `void drive()` at level `Warning`, app `"hil"`, call `warning("bus {}", 3)` is drained
- **THEN** the line parses as JSON with keys in order `timestamp, mono_ns, boot, level, app, thread_id, message`, `level` is `"warning"`, `mono_ns` and `thread_id` are numeric, `boot` is the 16-character boot token, and `message` starts `"[app.cpp:42] (void drive()) bus 3"` (basename only, no directory)

#### Scenario: Monotonic instant is the source of truth

- **WHEN** the same record's `timestamp` and `mono_ns` are compared after a UTC calibration change
- **THEN** `mono_ns` is unchanged and `timestamp` is the derived value for that instant

#### Scenario: Legacy lines remain readable

- **WHEN** a reader parses a line that has no `mono_ns` or `boot` field
- **THEN** it treats the line as the legacy format and relies on `timestamp` alone

#### Scenario: Escaping is correct and allocation-free

- **WHEN** a rendered message contains quotes, backslashes, or control characters
- **THEN** the emitted line is valid JSON with those characters escaped, and the writer performed no allocation

#### Scenario: Non-ASCII payloads are preserved

- **WHEN** a format string or argument contains Cyrillic UTF-8 text
- **THEN** the file line round-trips the same UTF-8 bytes inside valid JSON (no loss or corruption)

### Requirement: Nanosecond UTC timestamps via calibrated clocks

`FileLogger` SHALL render the UTC ISO-8601 value from the UTC offset the time base publishes for that instant, and SHALL NOT sample the wall clock itself. When no calibration covers the record's instant the writer SHALL still emit `mono_ns` and mark the base as uncalibrated rather than inventing a UTC value. Conversion SHALL happen on the writer thread using `nexigen::chrono` formatting, without calling `CLOCK_REALTIME` on any path.

#### Scenario: File timestamps track wall time

- **WHEN** a record is written shortly after a known `CLOCK_REALTIME` reading and a calibrated base
- **THEN** its JSON timestamp differs from that wall reading only by the expected drain/format latency (sub-millisecond tolerance on an idle system)

#### Scenario: Order within file matches monotonic order

- **WHEN** consecutive records are drained
- **THEN** their `mono_ns` values are non-decreasing and their formatted timestamps are non-decreasing

#### Scenario: Uncalibrated instant is explicit

- **WHEN** the time base publishes no UTC calibration covering a record's instant
- **THEN** the line still carries `mono_ns` and `boot`, and no fabricated UTC value is emitted

#### Scenario: Sink does not sample the wall clock

- **WHEN** the writer formats a record in steady state
- **THEN** it performs no `CLOCK_REALTIME` read and no heap allocation
