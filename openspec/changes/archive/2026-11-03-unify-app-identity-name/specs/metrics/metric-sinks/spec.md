# Spec Delta

## ADDED Requirements

### Requirement: Metric batches carry the application name

`JsonFileMetricSink` SHALL emit an `"app"` field in every batch JSON line, carrying the application name the sink was configured with. The field SHALL be written once per line alongside `"timestamp"` and `"records"`, escaped as JSON, and rendered from a fixed buffer copied at construction (no per-batch allocation).

#### Scenario: Batch line carries the app field

- **WHEN** a batch is written to a temporary file and read back line by line
- **THEN** each line parses to an object whose `"app"` field equals the configured application name

#### Scenario: Empty app name is omitted or empty

- **WHEN** the sink is configured with an empty application name
- **THEN** the line still parses, with either no `"app"` field or an empty `"app"` value, and no identity is imposed

## MODIFIED Requirements

### Requirement: File sink configuration serialization

`JsonFileMetricSinkConfig` SHALL implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization. It SHALL carry the sink path, append flag, and application name. Sink classes themselves SHALL render diagnostics but SHALL NOT be deserializable, because rebinding a sink performs I/O at construction.

#### Scenario: File sink configuration round-trip

- **WHEN** a `JsonFileMetricSinkConfig` with a path, append flag, and application name is serialized and parsed back
- **THEN** the path, append flag, and application name survive the round-trip under `JSON_NOEXCEPTION=1`

#### Scenario: File sink configuration diagnostics

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a `JsonFileMetricSinkConfig`
- **THEN** the member and free results are equal and non-empty, and the formatter renders the same text

#### Scenario: Rebinding is construction, not deserialization

- **WHEN** a translation unit attempts `from_json` into a `JsonFileMetricSink`
- **THEN** compilation fails because no overload exists
