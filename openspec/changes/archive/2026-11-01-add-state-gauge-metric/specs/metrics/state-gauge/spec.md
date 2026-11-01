# Spec Delta

## Purpose

Records a current named state — a bounded string such as a mode, phase, or enum spelling — as a last-writer-wins metric value observable through the metrics pipeline.

## ADDED Requirements

### Requirement: State recording surface

`nexigen::metrics::StateGauge<ConcurrencyPolicy, AccessPolicy, ResetPolicy>` SHALL support `set(std::string_view) noexcept -> bool`. `set` SHALL copy the value into fixed inline storage and return `true`, or reject with `false` (no side effect) when the value is empty or longer than 63 characters.

#### Scenario: Valid state is stored

- **WHEN** `set("active")` is called on a state gauge
- **THEN** it returns `true` and a subsequent `read()` returns `"active"`

#### Scenario: Empty state is rejected

- **WHEN** `set("")` is called
- **THEN** it returns `false` and the previous value is unchanged

#### Scenario: Over-long state is rejected

- **WHEN** `set` is called with a value longer than 63 characters
- **THEN** it returns `false` and the previous value is unchanged

### Requirement: State read surface

A `Readable` state gauge SHALL expose `read() const` returning the current value and `snapshot() const` returning a `MetricRecord` carrying `MetricKind::State` and the current value. `read()` and `snapshot()` SHALL be available only when `Access == AccessPolicy::Readable`.

#### Scenario: Read returns the current value

- **WHEN** a state gauge holds `"running"` and `read()` is called
- **THEN** the returned view equals `"running"`

#### Scenario: Snapshot carries kind and value

- **WHEN** `snapshot()` is called on a state gauge holding `"degraded"`
- **THEN** the record reports `MetricKind::State` and the value `"degraded"`

#### Scenario: Write-only gauge hides the read side

- **WHEN** a `WriteOnly` state gauge's surface is inspected
- **THEN** `read()` and `snapshot()` are unavailable at compile time

### Requirement: Single-threaded state storage

In `SingleThreaded` mode a state gauge SHALL store the value in plain (non-atomic) inline storage, targeting a bounded, allocation-free `set`. Reads and harvests SHALL NOT run concurrently with recording unless the caller establishes happens-before (for example, via `collect_now()` on the recording thread).

#### Scenario: Single-threaded set is allocation-free

- **WHEN** a test allocation arena is armed and `set` is called repeatedly on a `SingleThreaded` gauge
- **THEN** no heap allocation is observed

### Requirement: Multi-threaded last-writer-wins

In `MultiThreaded` mode a state gauge SHALL accept `set` from multiple threads with last-writer-wins semantics and publish each value through a seqlock, so a concurrent reader or harvester observes a complete value (never a torn mix of two writers' bytes) without locking or blocking.

#### Scenario: Concurrent sets never tear

- **WHEN** two threads repeatedly `set` distinct values while a third reads concurrently
- **THEN** every observed value equals one of the complete written values, and no sanitizer reports a data race

#### Scenario: Last writer is observable

- **WHEN** a final `set("final")` completes before a harvest
- **THEN** the harvested record carries `"final"`

### Requirement: State reset windows

With `ResetPolicy::Cumulative`, reads and harvests SHALL leave the stored value intact. With `ResetPolicy::ResetOnRead`, each read or harvest SHALL report the current value and then clear it to empty.

#### Scenario: Cumulative state persists

- **WHEN** a cumulative gauge is set and then read and harvested repeatedly
- **THEN** every observation reports the same value until the next `set`

#### Scenario: Reset-on-read clears after observation

- **WHEN** a reset-on-read gauge holds `"armed"` and is read
- **THEN** the read returns `"armed"` and the next read returns an empty view

### Requirement: State serialization contract

`StateGauge` SHALL provide a cold-path `std::string to_string() const` and free `to_string(const T&)`, a diagnostic `to_json` under `JSON_NOEXCEPTION=1`, and `std::formatter` support; it SHALL NOT provide `from_json`, preserving the Meter-only creation rule.

#### Scenario: State diagnostics render

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a gauge holding `"active"`
- **THEN** the member and free results are equal and non-empty, they include the value and policy names, and the formatter renders the same text

#### Scenario: State JSON is write-only

- **WHEN** `to_json` serializes a state gauge under `JSON_NOEXCEPTION=1` and a translation unit attempts `from_json`
- **THEN** the JSON describes the gauge's state, and the `from_json` attempt fails at compile time
