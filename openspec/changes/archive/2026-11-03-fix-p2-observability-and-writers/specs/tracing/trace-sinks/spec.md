# tracing/trace-sinks delta spec

## MODIFIED Requirements

### Requirement: JsonFileTraceSink JSONL output

`JsonFileTraceSink` SHALL write one complete JSON object per span per line (JSONL) with UTC ISO-8601 start and end timestamps (`YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` from `nexigen::chrono`), identifiers as zero-padded 16-character hexadecimal strings, kind/status as their canonical names, and attributes as typed key/value pairs; `flush()` SHALL synchronize the file, surfacing I/O failures as `SystemError`. A span line whose JSON does not fit the sink's fixed scratch SHALL be discarded as a whole (nothing partial is written), the counter of discarded span lines SHALL be incremented, and the count SHALL be visible through `to_string()`.

#### Scenario: One parseable JSON object per span per line

- **WHEN** drained records are written and the file is read back line by line and each line parsed as JSON
- **THEN** every line parses to one object carrying the span's ids, parent id, kind, status, thread id, both ISO-8601 timestamps, duration, name, source location, and attributes, and each field round-trips to the original record

#### Scenario: Timestamps render in UTC ISO-8601 shape

- **WHEN** a line's start and end timestamp fields are inspected
- **THEN** each matches `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` with a `Z` suffix and the nine-digit nanosecond remainder

#### Scenario: Attribute values serialize by their packed type

- **WHEN** a record carrying integer, unsigned, double, boolean, and string attributes is written and parsed back
- **THEN** integers and unsigneds are JSON numbers, doubles preserve their value, booleans are JSON booleans, and strings are JSON strings with proper escaping of quotes, backslashes, and control characters

#### Scenario: Open and write failures are visible

- **WHEN** the sink is constructed with an unopenable path, or a write fails mid-stream
- **THEN** the failure surfaces through `start`/`on_span`/`flush` as `SystemError` without emitting an invalid JSON line

#### Scenario: Scratch overflow discards the line and counts it

- **WHEN** a span's rendering exceeds the sink's fixed scratch so the writer's `ok()` goes false
- **THEN** no partial bytes are written, the write reports `core::ErrorCode::BufferFull`, and the sink's discarded-line counter (visible through `to_string()`) is incremented by exactly one per discarded line
