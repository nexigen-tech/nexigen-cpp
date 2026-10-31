# Spec Delta

## ADDED Requirements

### Requirement: Chrono settings use an explicit file pair

The system SHALL load chrono settings only from a caller-supplied `Chrono_settings_paths` (a global file and a local file; an empty path disables that source), reusing the chrono module's `load_chrono_settings`. It MUST NOT discover configuration implicitly.

#### Scenario: Chrono pair is supplied

- **WHEN** the application supplies a chrono path pair
- **THEN** chrono reads its own pair and reports its own `global_loaded`/`local_loaded` flags

#### Scenario: Missing chrono files are not failures

- **WHEN** any supplied chrono path is absent or empty
- **THEN** chrono reports that source as not loaded, records no diagnostic for it, and keeps the built-in defaults

## MODIFIED Requirements

### Requirement: App settings aggregate every subsystem

The system SHALL provide `App_settings` with `logging`, `metrics`, `tracing`, and `chrono` members, `App_settings_paths` carrying the four path pairs, and `load_app_settings(paths, out)` that loads all four. The aggregate status SHALL be `SystemError` if any supplied file could not be read, otherwise `InvalidArgument` if any diagnostic was recorded, otherwise OK.

#### Scenario: One call loads all three subsystems

- **WHEN** `load_app_settings` receives valid logging, metrics, tracing, and chrono pairs
- **THEN** the output contains merged options for all four and the status is OK

#### Scenario: Worst status is reported

- **WHEN** logging loads cleanly but a metrics file cannot be read
- **THEN** `load_app_settings` returns `SystemError` while still populating every subsystem it could read

### Requirement: App settings are serializable

`Settings_paths`, `Metrics_settings`, `Tracing_settings`, and `App_settings` SHALL provide member and free `to_string()` and a `std::formatter` specialization. `Settings_paths` SHALL provide non-throwing ADL `to_json`/`from_json`; loader-produced settings types SHALL provide `to_json` only and SHALL NOT provide `from_json`. `App_settings` serialization SHALL include the `chrono` member, whose own serialization is provided by the chrono module.

#### Scenario: Metrics settings render and serialize

- **WHEN** `to_string()` and `to_json` are called on a populated `Metrics_settings`
- **THEN** the text and JSON name every provider field plus the loaded flags and diagnostics

#### Scenario: Chrono settings render through App_settings

- **WHEN** `to_string()` and `to_json` are called on a populated `App_settings` carrying a `chrono` member
- **THEN** the text and JSON include the chrono provider fields and loaded flags

#### Scenario: Loader output has no from_json

- **WHEN** the test translation unit checks for `from_json` on `App_settings`
- **THEN** the overload is absent and a compile-time assertion pins that contract
