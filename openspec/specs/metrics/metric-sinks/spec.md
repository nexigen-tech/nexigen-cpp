# metrics/metric-sinks Specification

## Purpose

Defines the sink contract the provider harvests into, plus the null, console, JSON-file, and composite sink implementations that turn metric batches into diagnostics without virtual dispatch or hot-path allocation.

## Requirements

### Requirement: Metric sink concept

`nexigen::metrics::concepts::MetricSink` SHALL be a C++23 concept satisfied only by types offering `on_batch(const MetricBatchSnapshot&) noexcept -> core::Status` and `flush() noexcept -> core::Status`, with no virtual functions required.

#### Scenario: Conforming sinks satisfy the concept

- **WHEN** `concepts::MetricSink<T>` is evaluated for `NullMetricSink`, `ConsoleMetricSink`, `JsonFileMetricSink`, and a `CompositeMetricSink` over them
- **THEN** it is `true` in a constant expression

#### Scenario: Missing or wrong member rejects the concept

- **WHEN** a type lacks `on_batch`, lacks `flush`, or returns a non-`core::Status` type from either
- **THEN** `concepts::MetricSink<T>` is `false`

#### Scenario: Sink callbacks are allocation-free

- **WHEN** any sink processes a batch under an armed allocation arena
- **THEN** no `operator new`, `delete`, `malloc`, `free`, or dynamic container growth occurs

### Requirement: Null metric sink

`NullMetricSink` SHALL accept every batch and complete with success without reading metric fields, writing output, allocating, or performing system calls, and its `flush()` SHALL return success.

#### Scenario: Null sink is a true no-op

- **WHEN** a provider bound to `NullMetricSink` harvests a non-empty batch
- **THEN** `on_batch` returns success, no bytes are written, and no allocation is observed

#### Scenario: Null sink flush succeeds

- **WHEN** `NullMetricSink::flush()` is called
- **THEN** it returns a successful `core::Status`

#### Scenario: Null sink diagnostics contract

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a `NullMetricSink`
- **THEN** the member and free results are equal non-empty strings, the formatter renders the same text, and its `to_json`/`from_json` round-trip under `JSON_NOEXCEPTION=1`

### Requirement: Console metric sink

`ConsoleMetricSink` SHALL render each batch as human-readable lines and emit them through raw `::write` to a configured file descriptor (stdout by default), without iostreams or heap allocation. Partial writes and `EINTR` SHALL be retried, and an unrecoverable write error SHALL be returned as `core::ErrorCode::SystemError`.

#### Scenario: Batch is rendered and written

- **WHEN** a batch containing multiple records is passed to a console sink writing to a pipe or temporary file descriptor
- **THEN** the output contains each metric name and its scalar values in readable form, and `on_batch` returns success

#### Scenario: Write failures surface

- **WHEN** the configured descriptor is closed or returns an unrecoverable error
- **THEN** `on_batch` returns `core::ErrorCode::SystemError` and does not abort

#### Scenario: Console sink flush succeeds

- **WHEN** `flush()` is called on a console sink
- **THEN** it returns success after any pending bytes have been written

#### Scenario: Console sink diagnostics contract

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a `ConsoleMetricSink`
- **THEN** the rendering includes the destination descriptor, the member and free results agree, and its `to_json`/`from_json` round-trip the descriptor under `JSON_NOEXCEPTION=1`

### Requirement: JSON-lines file sink

`JsonFileMetricSink` SHALL be constructed from a configuration carrying a file path and append flag, open the file during initialization, and write one JSON object per batch per line with a UTC ISO-8601 timestamp rendered through `nexigen::chrono`. Each line SHALL be valid JSON written with raw `::write`; `flush()` SHALL push pending data to the file and report I/O failures as `core::Status`.

#### Scenario: Batch becomes one JSON line

- **WHEN** a batch is written to a temporary file and read back line by line
- **THEN** the file contains exactly one parseable JSON object for that batch, its timestamp matches the `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` shape, and its record array matches the batch contents

#### Scenario: JSON strings are escaped

- **WHEN** a metric or meter name contains quotes, backslashes, or control characters
- **THEN** the emitted line remains valid JSON and parses back to the original name

#### Scenario: Multiple batches append

- **WHEN** several batches are written to an append-mode sink
- **THEN** the file contains one line per batch in delivery order, and a fresh process can parse every line independently

#### Scenario: Open and write failures surface

- **WHEN** the path cannot be opened, or a write fails because the descriptor is closed
- **THEN** construction or `on_batch` reports the failure through `core::Status`, and no partial invalid JSON line is reported as success

#### Scenario: Flush persists pending data

- **WHEN** `flush()` is called after batches were written and the file is subsequently read
- **THEN** every delivered line is present in the file, and an I/O failure during flush returns `core::ErrorCode::SystemError`

### Requirement: File sink configuration serialization

`JsonFileMetricSink_config` SHALL implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization. Sink classes themselves SHALL render diagnostics but SHALL NOT be deserializable, because rebinding a sink performs I/O at construction.

#### Scenario: File sink configuration round-trip

- **WHEN** a `JsonFileMetricSink_config` with a path and append flag is serialized and parsed back
- **THEN** the path and append flag survive the round-trip under `JSON_NOEXCEPTION=1`

#### Scenario: File sink configuration diagnostics

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a `JsonFileMetricSink_config`
- **THEN** the member and free results are equal and non-empty, and the formatter renders the same text

#### Scenario: Rebinding is construction, not deserialization

- **WHEN** a translation unit attempts `from_json` into a `JsonFileMetricSink`
- **THEN** compilation fails because no overload exists

### Requirement: Composite metric sink fan-out

`CompositeMetricSink<Sinks...>` SHALL own a `std::tuple<Sinks...>`, forward every batch to every member without short-circuiting on failure, and provide `get<Index>()` and `get<SinkType>()` accessors guarded so absent types and out-of-range indices fail at compile time. `flush()` SHALL flush every member and return the first non-success status.

#### Scenario: Every sink receives every batch

- **WHEN** a composite of three recording sinks receives one batch
- **THEN** all three members observe that same batch exactly once

#### Scenario: One failing sink does not starve the rest

- **WHEN** the first member returns an error from `on_batch`
- **THEN** the remaining members still receive the batch, and the composite returns the first error

#### Scenario: Typed and indexed access

- **WHEN** `get<0>()` and `get<ConsoleMetricSink>()` are called on a composite containing that sink
- **THEN** both return a reference to the same member, and an absent type or out-of-range index fails to compile

#### Scenario: Composite flush aggregates

- **WHEN** `flush()` is called and one member fails
- **THEN** every member is flushed, the first failure is returned, and a fully successful flush returns success

#### Scenario: Composite diagnostics contract

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a composite sink
- **THEN** the rendering names its member count, the member and free results agree, `to_json` emits that description, and `from_json` fails at compile time for the variadic type
