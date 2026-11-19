# Spec Delta

## MODIFIED Requirements

### Requirement: JSON-lines file sink

`JsonFileMetricSink` SHALL be constructed from a configuration carrying a file path and append flag, open the file during initialization, and write one JSON object per batch per line carrying the canonical monotonic instant, the boot token, and a UTC ISO-8601 timestamp derived from the time base. Each line SHALL be valid JSON written with raw `::write`; `flush()` SHALL push pending data to the file and report I/O failures as `core::Status`. Scratch overflow is pinned by its own scenario as a counted, whole-batch discard.

#### Scenario: Batch becomes one JSON line

- **WHEN** a batch is written to a temporary file and read back line by line
- **THEN** the file contains exactly one parseable JSON object for that batch, it carries `timestamp` in the `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` shape, numeric `mono_ns`, a 16-character `boot` token, and a record array matching the batch contents

#### Scenario: Monotonic instant is the source of truth

- **WHEN** the same batch's `timestamp` and `mono_ns` are compared after a UTC calibration change
- **THEN** `mono_ns` is unchanged and `timestamp` is the derived value for that instant

#### Scenario: Legacy lines remain readable

- **WHEN** a reader parses a line that has no `mono_ns` or `boot` field
- **THEN** it treats the line as the legacy format and relies on `timestamp` alone

#### Scenario: JSON strings are escaped

- **WHEN** a metric or meter name contains quotes, backslashes, or control characters
- **THEN** the emitted line remains valid JSON and parses back to the original name

#### Scenario: State records serialize their value

- **WHEN** a batch contains a state record
- **THEN** the emitted record carries `"kind":"State"` and a `"value"` string equal to the recorded state, not a histogram payload

#### Scenario: Multiple batches append

- **WHEN** several batches are written to an append-mode sink
- **THEN** the file contains one line per batch in delivery order, and a fresh process can parse every line independently

#### Scenario: Open and write failures surface

- **WHEN** the path cannot be opened, or a write fails because the descriptor is closed
- **THEN** construction or `on_batch` reports the failure through `core::Status`, and no partial invalid JSON line is reported as success

#### Scenario: Flush persists pending data

- **WHEN** `flush()` is called after batches were written and the file is subsequently read
- **THEN** every delivered line is present in the file, and an I/O failure during flush returns `core::ErrorCode::SystemError`

#### Scenario: Scratch overflow discards the batch and counts it

- **WHEN** a batch's rendering exceeds the sink's fixed scratch so the writer's `ok()` goes false
- **THEN** no partial bytes are written, `on_batch` reports `core::ErrorCode::BufferFull`, and the sink's discarded-batch counter (visible through `to_string()`) is incremented by exactly one per discarded batch
