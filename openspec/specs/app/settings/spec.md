# app/settings Specification

## Purpose

Defines the application-level settings pipeline: the aggregate `Settings` value and `AppSettingsPaths` path pairs consumed by `Infrastructure::initialize`, assembled by delegating to the four subsystem settings loaders (logging, metrics, tracing, and chrono).

## Requirements

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

### Requirement: Settings loading is cold path only

App settings loading, validation, diagnostics, and serialization SHALL execute before `App::initialize` (or inside it before workers start) and SHALL NOT be called from a tick, producer, or worker hot path. Steady-state ticking SHALL retain zero allocation and no locking.

#### Scenario: Tick path stays allocation-free

- **WHEN** an application runs ticks after settings were loaded
- **THEN** the tick loop performs no settings parsing, JSON access, or allocation
