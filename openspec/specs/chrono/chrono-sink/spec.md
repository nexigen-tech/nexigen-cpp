# chrono/chrono-sink Specification

## Purpose

Defines the sink contract the clock service delivers snapshots into, plus null, console, JSON-file, and composite sink implementations that persist snapshots without virtual dispatch or hot-path allocation.

## Requirements

### Requirement: Chrono sink concept

`nexigen::chrono::concepts::ChronoSink` SHALL be a C++23 concept satisfied only by types offering `on_snapshot(const Chrono_snapshot&) noexcept -> core::Status` and `flush() noexcept -> core::Status`, with no virtual functions required.

#### Scenario: Conforming sinks satisfy the concept

- **WHEN** `concepts::ChronoSink<T>` is evaluated for `NullChronoSink`, `ConsoleChronoSink`, `JsonFileChronoSink`, and a `CompositeChronoSink` over them
- **THEN** it is `true` in a constant expression

#### Scenario: Missing or wrong member rejects the concept

- **WHEN** a type lacks `on_snapshot`, lacks `flush`, or returns a non-`core::Status` type from either
- **THEN** `concepts::ChronoSink<T>` is `false`

#### Scenario: Sink callbacks are allocation-free

- **WHEN** any sink processes a snapshot under an armed allocation arena
- **THEN** no `operator new`, `delete`, `malloc`, `free`, or dynamic container growth occurs

### Requirement: Null chrono sink

`NullChronoSink` SHALL accept every snapshot and complete with success without reading snapshot fields, writing output, allocating, or performing system calls, and its `flush()` SHALL return success.

#### Scenario: Null sink is a true no-op

- **WHEN** a provider bound to `NullChronoSink` delivers a snapshot
- **THEN** `on_snapshot` returns success, no bytes are written, and no allocation is observed

#### Scenario: Null sink flush succeeds

- **WHEN** `NullChronoSink::flush()` is called
- **THEN** it returns a successful `core::Status`

#### Scenario: Null sink diagnostics contract

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a `NullChronoSink`
- **THEN** the member and free results are equal non-empty strings, the formatter renders the same text, and its `to_json`/`from_json` round-trip under `JSON_NOEXCEPTION=1`

### Requirement: Console chrono sink

`ConsoleChronoSink` SHALL render each snapshot as a human-readable line and emit it through raw `::write` to a configured file descriptor (stdout by default), without iostreams or heap allocation. Partial writes and `EINTR` SHALL be retried, and an unrecoverable write error SHALL be returned as `core::ErrorCode::SystemError`.

#### Scenario: Snapshot is rendered and written

- **WHEN** a snapshot is passed to a console sink writing to a pipe or temporary file descriptor
- **THEN** the output contains the monotonic-raw stamp and the UTC offset in readable form, and `on_snapshot` returns success

#### Scenario: Write failures surface

- **WHEN** the configured descriptor is closed or returns an unrecoverable error
- **THEN** `on_snapshot` returns `core::ErrorCode::SystemError` and does not abort

#### Scenario: Console sink flush succeeds

- **WHEN** `flush()` is called on a console sink
- **THEN** it returns success after any pending bytes have been written

#### Scenario: Console sink diagnostics contract

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a `ConsoleChronoSink`
- **THEN** the rendering includes the destination descriptor, the member and free results agree, and its `to_json`/`from_json` round-trip the descriptor under `JSON_NOEXCEPTION=1`

### Requirement: JSON-file chrono sink

`JsonFileChronoSink` SHALL be constructed from a configuration carrying a file path and an append flag, open the file during initialization, and write one JSON object per snapshot per line using raw `::write`; `flush()` SHALL push pending data to the file and report I/O failures as `core::Status`.

#### Scenario: Snapshot becomes one JSON line

- **WHEN** a snapshot is written to a temporary file and read back line by line
- **THEN** the file contains exactly one parseable JSON object for that snapshot whose fields reproduce the stamp and the UTC offset

#### Scenario: Multiple snapshots append

- **WHEN** several snapshots are written to an append-mode sink
- **THEN** the file contains one line per snapshot in delivery order, and a fresh process can parse every line independently

#### Scenario: Open and write failures surface

- **WHEN** the path cannot be opened, or a write fails because the descriptor is closed
- **THEN** construction or `on_snapshot` reports the failure through `core::Status`, and no partial invalid JSON line is reported as success

#### Scenario: Flush persists pending data

- **WHEN** `flush()` is called after snapshots were written and the file is subsequently read
- **THEN** every delivered line is present in the file, and an I/O failure during flush returns `core::ErrorCode::SystemError`

### Requirement: File sink configuration serialization

`JsonFileChronoSink_config` SHALL implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization. Sink classes themselves SHALL render diagnostics but SHALL NOT be deserializable, because rebinding a sink performs I/O at construction.

#### Scenario: File sink configuration round-trip

- **WHEN** a `JsonFileChronoSink_config` with a path and append flag is serialized and parsed back
- **THEN** the path and append flag survive the round-trip under `JSON_NOEXCEPTION=1`

#### Scenario: File sink configuration diagnostics

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a `JsonFileChronoSink_config`
- **THEN** the member and free results are equal and non-empty, and the formatter renders the same text

#### Scenario: Rebinding is construction, not deserialization

- **WHEN** a translation unit attempts `from_json` into a `JsonFileChronoSink`
- **THEN** compilation fails because no overload exists

### Requirement: Composite chrono sink fan-out

`CompositeChronoSink<Sinks...>` SHALL own a `std::tuple<Sinks...>`, forward every snapshot to every member without short-circuiting on failure, and provide `get<Index>()` and `get<SinkType>()` accessors guarded so absent types and out-of-range indices fail at compile time. `flush()` SHALL flush every member and return the first non-success status.

#### Scenario: Every sink receives every snapshot

- **WHEN** a composite of three recording sinks receives one snapshot
- **THEN** all three members observe that same snapshot exactly once

#### Scenario: One failing sink does not starve the rest

- **WHEN** the first member returns an error from `on_snapshot`
- **THEN** the remaining members still receive the snapshot, and the composite returns the first error

#### Scenario: Typed and indexed access

- **WHEN** `get<0>()` and `get<ConsoleChronoSink>()` are called on a composite containing that sink
- **THEN** both return a reference to the same member, and an absent type or out-of-range index fails to compile

#### Scenario: Composite flush aggregates

- **WHEN** `flush()` is called and one member fails
- **THEN** every member is flushed, the first failure is returned, and a fully successful flush returns success

#### Scenario: Composite diagnostics contract

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a composite sink
- **THEN** the rendering names its member count, the member and free results agree, `to_json` emits that description, and `from_json` fails at compile time for the variadic type
