# Spec Delta

## ADDED Requirements

### Requirement: Application name is app-level

`Settings` SHALL carry an `app_name` member holding the application's telemetry name, supplied by the caller (from the command line) and applied to the logger, metrics sink, and trace sink during initialization. A default-constructed `Settings` SHALL leave `app_name` empty; an empty `app_name` SHALL mean no application identity is imposed.

#### Scenario: App name is set on the aggregate

- **WHEN** a populated `Settings` carries a non-empty `app_name`
- **THEN** the member equals the supplied value and `to_string()` names it

#### Scenario: Empty app name is allowed

- **WHEN** `Settings` is default-constructed
- **THEN** `app_name` is empty and no application identity is imposed

#### Scenario: App name renders and serializes

- **WHEN** a `Settings` with a non-empty `app_name` is rendered with `to_string()`, `std::format("{}", settings)`, and `to_json`
- **THEN** the text and JSON include the `app_name` value, and the formatter output equals `to_string()`
