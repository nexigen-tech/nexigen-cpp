# Spec Delta

## Purpose

Defines the viewer's offline contract: saving a selection in interchangeable formats, reopening saved and foreign log files, keeping boot sessions honest, and persisting the view configuration safely.

## ADDED Requirements

### Requirement: Save selected records

The viewer SHALL save selected records as JSONL using the canonical `nexigen-logd` field names, as CSV, or as plain text. It SHALL escape quotes, backslashes, and control characters so the JSONL remains valid for any message content. The write SHALL be asynchronous with progress.

#### Scenario: JSONL is interchangeable

- **WHEN** selected records are saved as JSONL
- **THEN** the file uses the same field names as `nexigen-logd` and can be reopened by the viewer

#### Scenario: Special characters

- **WHEN** a message contains a quote, a backslash, or a newline
- **THEN** the exported JSONL escapes it and parses back to the same text

#### Scenario: CSV and text

- **WHEN** the user selects CSV or TXT
- **THEN** the file is produced with correct CSV quoting or plain readable columns

### Requirement: Open a saved or foreign log

The viewer SHALL open its own JSONL, a `nexigen-logd` JSONL file, and a plain-text log. Parsing SHALL be chunked with progress, a partial trailing line SHALL wait for completion, and a malformed line SHALL be counted and skipped without aborting the import.

#### Scenario: Import a daemon file

- **WHEN** a JSONL file written by `nexigen-logd` is opened
- **THEN** its records appear in the history with their timestamps, applications, levels, and messages

#### Scenario: Malformed line

- **WHEN** a line is not valid JSON
- **THEN** it is skipped, counted, and the remaining lines are imported

#### Scenario: Plain text

- **WHEN** a plain-text log is opened
- **THEN** each complete line becomes a record with an empty application and an inferred level where possible

#### Scenario: Open while live

- **WHEN** the user opens a file while live capture is running
- **THEN** the viewer asks before replacing the live history

### Requirement: Boot sessions are not mixed silently

Imported records SHALL carry their boot identifier. When records from different boot sessions are loaded, the viewer SHALL warn and SHALL NOT present their monotonic deltas as comparable.

#### Scenario: Same boot

- **WHEN** a file from the current boot is opened
- **THEN** deltas and Since-Boot time are computed normally

#### Scenario: Mixed boots

- **WHEN** records from two different boots are present
- **THEN** the viewer warns and disables or flags cross-session deltas

### Requirement: Configuration persists safely

The viewer SHALL persist colors, filters, timestamp and delta modes, anomaly threshold, layout, memory budget, CPU limit, and service name as JSON, restore them at startup, fall back to defaults with a warning when the file is corrupt, and provide a reset. The configuration value types SHALL provide `to_string()`, `std::formatter`, and non-throwing `to_json`/`from_json`.

#### Scenario: Round-trip

- **WHEN** the viewer writes and then reads its configuration
- **THEN** every setting is restored

#### Scenario: Corrupt configuration

- **WHEN** the configuration file is malformed or contains wrong types
- **THEN** the viewer starts with defaults, warns once, and leaves the bad file untouched

#### Scenario: Diagnostic formats

- **WHEN** a configuration value is formatted with `std::format("{}", value)`
- **THEN** the output equals its `to_string()`, and its JSON round-trip preserves every field

### Requirement: Offline launch

The viewer SHALL support opening a file without any transport (`--offline`), so analysis and automated tests run without iceoryx2.

#### Scenario: Offline starts without COM

- **WHEN** the viewer is launched with a file and `--offline`
- **THEN** it opens the file, renders it, and does not attempt to attach to the log service

#### Scenario: Live is unavailable offline

- **WHEN** offline mode is active
- **THEN** the live status is shown as unavailable rather than as a connection error
