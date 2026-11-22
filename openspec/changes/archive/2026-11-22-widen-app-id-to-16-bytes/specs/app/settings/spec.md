# Spec Delta

## ADDED Requirements

### Requirement: Application identifier is app-level

`Settings` SHALL carry an `app_id` member holding the application's canonical 16-byte telemetry
identifier, supplied by the caller from the command line and applied to the logger, metrics sink,
and trace sink during initialization. A default-constructed `Settings` SHALL leave the identifier
unset; an unset identifier SHALL mean the selected policy keeps its own fallback identity.

#### Scenario: Identifier is set on the aggregate

- **WHEN** a populated `Settings` carries the identifier `CN_LOOP_1`
- **THEN** the member renders as `CN_LOOP_1` and `to_string()` names it

#### Scenario: Unset identifier is allowed

- **WHEN** `Settings` is default-constructed
- **THEN** `app_id` is unset and no runtime identity is imposed on the policy

#### Scenario: Identifier reaches every sink

- **WHEN** `Infrastructure` initializes a policy with a `Settings` carrying `RECV`
- **THEN** the logger, metric sink, and trace sink are configured with `RECV`

#### Scenario: Identifier renders and serializes

- **WHEN** a `Settings` with a set `app_id` is rendered with `to_string()`, `std::format("{}", settings)`, and `to_json`
- **THEN** the text and JSON include the identifier, and the formatter output equals `to_string()`
