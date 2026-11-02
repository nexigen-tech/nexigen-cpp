# Spec Delta

## ADDED Requirements

### Requirement: Trace spans carry the application name

`JsonFileTraceSink` SHALL emit an `"app"` field in every span JSON line, carrying the application name the sink was configured with. The field SHALL be escaped as JSON and rendered from a fixed buffer copied at construction (no per-span allocation).

#### Scenario: Span line carries the app field

- **WHEN** drained records are written and the file is read back line by line and each line parsed as JSON
- **THEN** each line parses to an object whose `"app"` field equals the configured application name

#### Scenario: Empty app name is omitted or empty

- **WHEN** the sink is configured with an empty application name
- **THEN** each line still parses, with either no `"app"` field or an empty `"app"` value, and no identity is imposed

## MODIFIED Requirements

### Requirement: Sink configuration serialization

Every sink configuration type SHALL follow the tracing-wide serialization contract: member and free `to_string`, full non-throwing `to_json`/`from_json` round-trips under `JSON_NOEXCEPTION=1`, and `std::formatter` rendering the `to_string()` text. The `JsonFileTraceSink` configuration SHALL additionally carry the application name.

#### Scenario: Sink configs round-trip through JSON

- **WHEN** a `JsonFileTraceSink` configuration (path, rotation policy, rate limit, application name) and a `ChromeTraceSink` configuration (path) are serialized and parsed back
- **THEN** every field survives the round-trip under `JSON_NOEXCEPTION=1`, and `std::format` renders each config's `to_string()` text
