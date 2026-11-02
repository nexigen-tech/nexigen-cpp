# Spec Delta

## MODIFIED Requirements

### Requirement: JSON-lines file sink

`JsonFileMetricSink` SHALL be constructed from a configuration carrying a file path and append flag, open the file during initialization, and write one JSON object per batch per line with a UTC ISO-8601 timestamp rendered through `nexigen::chrono`. Each line SHALL be valid JSON written with raw `::write`; `flush()` SHALL push pending data to the file and report I/O failures as `core::Status`.

#### Scenario: Batch becomes one JSON line

- **WHEN** a batch is written to a temporary file and read back line by line
- **THEN** the file contains exactly one parseable JSON object for that batch, its timestamp matches the `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` shape, and its record array matches the batch contents

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
