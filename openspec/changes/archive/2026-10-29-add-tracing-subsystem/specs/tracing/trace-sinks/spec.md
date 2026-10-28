# Spec Delta

## Purpose

Defines the span sinks: console rendering, rotating rate-limited JSONL files with UTC ISO-8601 timestamps, the Chrome Trace Event / Perfetto-compatible JSON format, and variadic composite fan-out.

## ADDED Requirements

### Requirement: ConsoleTraceSink

`ConsoleTraceSink` SHALL render each drained span as one human-readable line to stdout through fixed-size stack buffers (no iostreams, no per-record allocation), writing with `::write` retrying `EINTR` and partial writes; unrecoverable write failures surface as `SystemError` from `on_span` without aborting, and `flush()` is a no-op success.

#### Scenario: Console lines carry span identity and detail

- **WHEN** drained records are delivered to a console sink captured through a pipe fixture
- **THEN** each span renders one line containing its trace id, span id, parent id, kind, status, name, duration, and every attribute key/value

#### Scenario: Write failures surface without abort

- **WHEN** the sink's descriptor is closed and a record is delivered
- **THEN** `on_span` returns `SystemError`, the process does not abort under `-fno-exceptions`, and the sink stays usable for error reporting

#### Scenario: Console rendering allocates nothing per record

- **WHEN** records are delivered in a loop with the allocation counter armed
- **THEN** zero heap allocations occur per delivered record

### Requirement: JsonFileTraceSink JSONL output

`JsonFileTraceSink` SHALL write one complete JSON object per span per line (JSONL) with UTC ISO-8601 start and end timestamps (`YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` from `nexigen::chrono`), identifiers as zero-padded 16-character hexadecimal strings, kind/status as their canonical names, and attributes as typed key/value pairs; `flush()` SHALL synchronize the file, surfacing I/O failures as `SystemError`.

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

### Requirement: JsonFileTraceSink rotation and rate limiting

`JsonFileTraceSink` SHALL support hourly and daily rotation policies: when the UTC window of incoming spans changes, the sink closes the current file and opens the next, named by the configured path prefix plus the window's UTC hour or date; and SHALL support an output rate limit — a configurable maximum spans per window — where spans beyond the budget are not written and counted, and a drop summary line with the exact count is emitted at the window boundary before further records land.

#### Scenario: Rotation files are named by their UTC window

- **WHEN** spans are drained under the `Hourly` policy, and separately under the `Daily` policy
- **THEN** the active file's name embeds the current UTC hour (and date) for `Hourly`, and the current UTC date for `Daily`, and spans continue into a new file after the window changes

#### Scenario: Rotation can be disabled

- **WHEN** the rotation policy is `None`
- **THEN** all spans land in the single configured file for the sink's lifetime

#### Scenario: Rate limiting drops with an exact summary

- **WHEN** a burst exceeding the configured per-window budget is drained and the window then rolls over
- **THEN** only the budgeted spans are written as JSON lines, the excess is not written, and the file contains a drop summary line naming the exact dropped count before any subsequent window's records

#### Scenario: Unthrottled sinks write everything

- **WHEN** no rate limit is configured
- **THEN** every drained span lands in the file and no summary lines appear

### Requirement: ChromeTraceSink Perfetto-compatible output

`ChromeTraceSink` SHALL produce a Chrome Trace Event format JSON file: a single parseable JSON object whose `traceEvents` array holds one complete (`"ph": "X"`) event per span with `name`, `cat` (the span kind's canonical name), `ts` and `dur` in integer microseconds, `pid`, `tid`, and an `args` object carrying the trace id, span id, parent span id as hexadecimal strings and every span attribute — so the file loads and renders an execution timeline in Perfetto (`ui.perfetto.dev`).

#### Scenario: The file parses as one Chrome trace object

- **WHEN** a sink is started, spans drain into it, and it is stopped, and the whole file is parsed as a single JSON value
- **THEN** it parses to an object with a `traceEvents` array holding exactly one event per drained span

#### Scenario: Events carry complete-event fields

- **WHEN** an event is inspected
- **THEN** it has `"ph": "X"`, the span's name as `name`, the kind's canonical name as `cat`, integer `ts`/`dur` derived from the record's timestamps, the process id as `pid`, the record's cached thread id as `tid`, and `args` with `trace_id`/`span_id`/`parent_span_id` hexadecimal strings plus typed attribute values

#### Scenario: Timestamps are relative to the first event

- **WHEN** the first span of the trace is inspected and a later span's `ts` is compared
- **THEN** the first event's `ts` is 0, later events' `ts` values are non-negative integer microseconds relative to it, and each `dur` equals the span's end-minus-start duration to microsecond truncation

#### Scenario: Nested and cross-thread timelines are coherent

- **WHEN** a parent and child span pair (including one linked across threads) drains through the sink
- **THEN** their `args` ids show the child carrying the parent's trace id and span id, so the timeline visualizes the causal chain in Perfetto

### Requirement: CompositeTraceSink fan-out

`CompositeTraceSink<Sinks...>` SHALL forward every span to every member sink without short-circuiting — one member's failure never starves the others, with the first error surfacing after all members were attempted — and SHALL aggregate `flush` across members while offering compile-time-indexed and type-based member access.

#### Scenario: Every member receives the same record

- **WHEN** a record drains through a composite of a console and a file sink
- **THEN** the console fixture shows the span's line and the file contains the span's JSON object — both members received it

#### Scenario: A failing member does not starve the rest

- **WHEN** the composite's first member returns `SystemError` for a record
- **THEN** the remaining members still receive that record and their output is unaffected, and the composite surfaces the first error

#### Scenario: Members are reachable by index and type

- **WHEN** `get<Index>()` and `get<SinkType>()` are used on a composite
- **THEN** both return the same member object, and an out-of-range index or absent type fails compilation

### Requirement: Sink configuration serialization

Every sink configuration type SHALL follow the tracing-wide serialization contract: member and free `to_string`, full non-throwing `to_json`/`from_json` round-trips under `JSON_NOEXCEPTION=1`, and `std::formatter` rendering the `to_string()` text.

#### Scenario: Sink configs round-trip through JSON

- **WHEN** a `JsonFileTraceSink` configuration (path, rotation policy, rate limit) and a `ChromeTraceSink` configuration (path) are serialized and parsed back
- **THEN** every field survives the round-trip under `JSON_NOEXCEPTION=1`, and `std::format` renders each config's `to_string()` text

### Requirement: RotationPolicy enum contract

The `RotationPolicy` enum (`None`, `Hourly`, `Daily`) SHALL provide constexpr `to_string`/`from_string`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization honoring fill, alignment, and width.

#### Scenario: RotationPolicy follows the enum contract

- **WHEN** `to_string`/`from_string`, `to_json`/`from_json`, and `std::format` are exercised for every `RotationPolicy` enumerator, plus an out-of-range cast and hostile JSON
- **THEN** names round-trip (`"None"`, `"Hourly"`, `"Daily"`), out-of-range renders `"Unknown"`, hostile JSON leaves the value unchanged without aborting, and `std::format` renders the canonical names honoring width and alignment

### Requirement: Live sink serialization surfaces

Live sink classes SHALL provide member and free `to_string()` and `std::formatter`, plus `to_json` serializing their configuration and state — and deliberately no `from_json`, since sinks are constructed from configuration objects, not parsed from data.

#### Scenario: Live sinks render diagnostics and refuse deserialization

- **WHEN** `to_string()` and `std::format` are evaluated for each sink class and a translation unit attempts `from_json` into a sink
- **THEN** both render the sink's configuration and state, and the deserialization fails at compile time
