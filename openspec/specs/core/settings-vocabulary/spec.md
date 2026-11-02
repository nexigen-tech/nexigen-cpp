# core/settings-vocabulary Specification

## Purpose

Defines the shared settings-loading vocabulary — the `SettingsSource` enum and the `SettingsPaths` / `SettingsDiagnostic` structs — that every subsystem settings loader uses to name configuration sources, carry explicit global/local file pairs, and report best-effort findings.

## Requirements

### Requirement: Settings source enumerates the merge layers

The system SHALL provide a `SettingsSource` enum with `Global`, `Local`, `Environment`, and `Computed` enumerators identifying which layer produced a setting value or a diagnostic. It SHALL provide `to_string`/`from_string` with the canonical lowercase names `global`, `local`, `environment`, `computed`, non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization.

#### Scenario: Source names round-trip through text and JSON

- **WHEN** each `SettingsSource` value is rendered with `to_string`, formatted with `std::format`, written with `to_json`, and read back with `from_json`
- **THEN** the canonical lowercase name is recovered and the enumerator round-trips to the original value

#### Scenario: Unknown source spelling leaves the target unchanged

- **WHEN** `from_string` or `from_json` receives a name that is not a canonical enumerator spelling
- **THEN** `from_string` returns false (or `from_json` leaves the target unchanged) and no exception path is taken

### Requirement: Settings paths carry an explicit global/local pair

The system SHALL provide a `SettingsPaths` struct with `global_file` and `local_file` string members naming the two explicit source files; an empty path SHALL disable that source. It SHALL provide member and free `to_string()`, a `std::formatter` specialization, and non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`.

#### Scenario: Paths render their two files

- **WHEN** `to_string()` is called on a `SettingsPaths` with a global and a local file
- **THEN** the text names both file paths

#### Scenario: Paths round-trip through JSON

- **WHEN** a `SettingsPaths` is written with `to_json` and read back with `from_json`
- **THEN** both file paths equal the originals

#### Scenario: Hostile JSON leaves paths unchanged

- **WHEN** `from_json` receives a non-object or a mistyped field
- **THEN** the target is left unchanged and no exception path is taken

### Requirement: Settings diagnostic records a finding

The system SHALL provide a `SettingsDiagnostic` struct with a `SettingsSource` `source`, a `field` string, and a `message` string. It SHALL provide member and free `to_string()`, a `std::formatter` specialization, and non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`.

#### Scenario: Diagnostic renders source, field, and message

- **WHEN** `to_string()` is called on a diagnostic
- **THEN** the text names the source, the field, and the message

#### Scenario: Diagnostic round-trips through JSON

- **WHEN** a diagnostic is written with `to_json` and read back with `from_json`
- **THEN** the source, field, and message equal the originals

#### Scenario: Hostile JSON leaves the diagnostic unchanged

- **WHEN** `from_json` receives a non-object or a mistyped field
- **THEN** the target is left unchanged and no exception path is taken

### Requirement: Settings vocabulary is cold path only

`SettingsSource`, `SettingsPaths`, and `SettingsDiagnostic` SHALL be cold-path configuration types used only during initialization, diagnostics, and serialization. They MAY allocate (`std::string`); they SHALL NOT be constructed, read, or serialized on a producer, tick, snapshot, or sink hot path.

#### Scenario: Hot paths are untouched

- **WHEN** a subsystem is initialized from loaded settings and then runs its steady-state loop
- **THEN** no settings-vocabulary type is constructed or serialized inside the loop
