# Spec Delta

## MODIFIED Requirements

### Requirement: App settings aggregate every subsystem

The system SHALL provide `Settings` with `logging`, `metrics`, `tracing`, and `chrono` members (of type `logging::Settings`, `metrics::Settings`, `tracing::Settings`, and `chrono::Settings` respectively), `AppSettingsPaths` carrying the four path pairs, and `load_app_settings(paths, out)` that loads all four by delegating to the four subsystem loaders. The aggregate status SHALL be `SystemError` if any supplied file could not be read, otherwise `InvalidArgument` if any diagnostic was recorded, otherwise OK.

#### Scenario: One call loads all three subsystems

- **WHEN** `load_app_settings` receives valid logging, metrics, tracing, and chrono pairs
- **THEN** the output contains merged options for all four and the status is OK

#### Scenario: Worst status is reported

- **WHEN** logging loads cleanly but a metrics file cannot be read
- **THEN** `load_app_settings` returns `SystemError` while still populating every subsystem it could read

### Requirement: App settings are serializable

`AppSettingsPaths` and `Settings` SHALL provide member and free `to_string()` and a `std::formatter` specialization. `AppSettingsPaths` SHALL provide non-throwing ADL `to_json`/`from_json`; the aggregate `Settings` SHALL provide `to_json` only and SHALL NOT provide `from_json`, because it is reconstructed by the loader. `Settings` serialization SHALL include the `metrics`, `tracing`, and `chrono` members, whose own serialization is provided by their modules.

#### Scenario: Metrics settings render and serialize

- **WHEN** `to_string()` and `to_json` are called on a populated `Settings` carrying a `metrics` member
- **THEN** the text and JSON name every metrics provider field plus the loaded flags and diagnostics

#### Scenario: Chrono settings render through App_settings

- **WHEN** `to_string()` and `to_json` are called on a populated `Settings` carrying a `chrono` member
- **THEN** the text and JSON include the chrono provider fields and loaded flags

#### Scenario: Loader output has no from_json

- **WHEN** the test translation unit checks for `from_json` on `Settings`
- **THEN** the overload is absent and a compile-time assertion pins that contract

## REMOVED Requirements

### Requirement: Metrics and tracing use explicit file pairs

**Reason**: Per-subsystem explicit-source behavior now lives in each module's own settings spec (`metrics/metrics-settings`, `tracing/tracing-settings`); the app layer only aggregates.
**Migration**: See `metrics/metrics-settings` "Settings sources are explicit" and `tracing/tracing-settings` "Settings sources are explicit".

### Requirement: Chrono settings use an explicit file pair

**Reason**: Chrono's explicit-source behavior is already owned by `chrono/chrono-settings` "Settings sources are explicit"; the app-level statement is redundant with the aggregate requirement above.
**Migration**: See `chrono/chrono-settings` "Settings sources are explicit".

### Requirement: Metrics settings map every provider field

**Reason**: Metrics field mapping moved to the metrics module along with `metrics::Settings`.
**Migration**: See `metrics/metrics-settings` "Metrics settings map every provider field".

### Requirement: Tracing settings map every provider field

**Reason**: Tracing field mapping moved to the tracing module along with `tracing::Settings`.
**Migration**: See `tracing/tracing-settings` "Tracing settings map every provider field".

### Requirement: Merge precedence matches logging

**Reason**: Merge precedence is now each subsystem's own behavior, specified in its module settings spec.
**Migration**: See `metrics/metrics-settings` and `tracing/tracing-settings` "Merge precedence matches the other subsystems".

### Requirement: Environment overrides are documented and typed

**Reason**: The metrics and tracing environment overrides moved to their module settings specs.
**Migration**: See `metrics/metrics-settings` and `tracing/tracing-settings` "Environment overrides are documented and typed".

### Requirement: Invalid input degrades to best effort

**Reason**: Per-subsystem best-effort loading is now specified in each module's settings spec.
**Migration**: See `metrics/metrics-settings` and `tracing/tracing-settings` "Invalid input degrades to best effort".

### Requirement: Metrics and tracing settings carry a sink member

**Reason**: The sink members moved to `metrics::Settings` and `tracing::Settings` in their modules.
**Migration**: See `metrics/metrics-settings` and `tracing/tracing-settings` "… settings carry a sink member".

### Requirement: Sink objects read the existing file-sink keys

**Reason**: Sink key handling moved to the metrics and tracing module settings specs.
**Migration**: See `metrics/metrics-settings` and `tracing/tracing-settings` "Sink object reads the file-sink keys".

### Requirement: Sink fields merge with documented precedence

**Reason**: Sink merge precedence moved to the metrics and tracing module settings specs.
**Migration**: See `metrics/metrics-settings` and `tracing/tracing-settings` "Sink fields merge with documented precedence".

### Requirement: Invalid sink configuration degrades to best effort

**Reason**: Sink validation moved to the metrics and tracing module settings specs.
**Migration**: See `metrics/metrics-settings` and `tracing/tracing-settings` "Invalid sink configuration degrades to best effort".

### Requirement: Sink configuration is part of settings serialization

**Reason**: Sink serialization is now owned by `metrics::Settings` and `tracing::Settings`; the aggregate includes it through its members.
**Migration**: See `metrics/metrics-settings` and `tracing/tracing-settings` "… settings are serializable".
