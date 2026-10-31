# Spec Delta

## Purpose

Loads explicitly supplied global and local JSON settings for the chrono provider, merges them with documented precedence, and turns the merged result into a `ChronoProvider_config` without touching any hot path.

## ADDED Requirements

### Requirement: Settings sources are explicit

The system SHALL read chrono settings only from source paths supplied by the caller: an optional global JSON file and an optional local JSON file. It MUST NOT consult implicit paths, environment-dependent directories, or files discovered by scanning the filesystem.

#### Scenario: Caller supplies both paths

- **WHEN** `load_chrono_settings` is called with existing global and local paths
- **THEN** both files are parsed in that order and the result reports `global_loaded` and `local_loaded` as true

#### Scenario: Missing source is not a failure

- **WHEN** either supplied path does not exist or an empty path is supplied
- **THEN** the load completes with the corresponding loaded flag false, no diagnostic for that source, and the other source or defaults still apply

#### Scenario: Empty paths yield defaults

- **WHEN** both supplied paths are empty
- **THEN** the result carries default values and reports neither source as loaded

### Requirement: Merge precedence is deterministic

Merged settings SHALL follow the precedence built-in defaults, then global file, then local file, then environment overrides. A field absent from a higher-priority source SHALL inherit the value from the lower-priority source, and a field absent from every source SHALL keep the default value.

#### Scenario: Local overrides global

- **WHEN** the global file sets `collect_interval` to one value and the local file sets it to another
- **THEN** the merged `collect_interval` equals the local value

#### Scenario: Absent local field inherits global

- **WHEN** the global file sets `core_mask` and the local file omits it
- **THEN** the merged `core_mask` equals the global value

#### Scenario: Environment has highest priority

- **WHEN** a file sets `core_mask` and the corresponding environment override sets another value
- **THEN** the merged `core_mask` equals the environment value

### Requirement: Environment overrides are documented and typed

The system SHALL apply these environment overrides, each parsed with the same validation as the JSON field it replaces: `NEXIGEN_CHRONO_COLLECT_INTERVAL_NS` (a non-negative nanosecond interval) and `NEXIGEN_CHRONO_CORE_MASK` (a non-negative 64-bit core mask). Unset variables SHALL leave the lower-priority value untouched.

#### Scenario: Interval override parses nanoseconds

- **WHEN** `NEXIGEN_CHRONO_COLLECT_INTERVAL_NS=1000000000` and no higher-priority source applies
- **THEN** the merged `collect_interval` is one second

#### Scenario: Core mask override parses an integer

- **WHEN** `NEXIGEN_CHRONO_CORE_MASK=5` and no higher-priority source applies
- **THEN** the merged `core_mask` equals `5`

#### Scenario: Unset variables do not erase file values

- **WHEN** a file sets `core_mask` and `NEXIGEN_CHRONO_CORE_MASK` is unset
- **THEN** the merged `core_mask` equals the file value

### Requirement: Invalid input degrades to best effort

The loader SHALL be best effort: a missing file, malformed JSON, a field of the wrong type, or an unparsable value SHALL be recorded as a diagnostic and SHALL leave the last valid or default value for that field. The loader MUST still populate merged settings and MUST NOT abort, throw, or require the caller to stop provider initialization.

#### Scenario: Malformed JSON is reported but does not stop the load

- **WHEN** the local file contains invalid JSON while the global file is valid
- **THEN** the call returns a failure `Status`, the diagnostics name the local source and parse problem, and the merged settings still contain the global values

#### Scenario: Invalid value keeps the previous value

- **WHEN** a file sets `collect_interval` to a negative or unparsable value after a valid source set it
- **THEN** a diagnostic is recorded and `collect_interval` remains the previous valid value

#### Scenario: Missing files produce no diagnostics

- **WHEN** every supplied path is absent
- **THEN** the returned `Status` is OK and the diagnostic list is empty

### Requirement: Settings diagnostics are serializable

`Chrono_settings_paths` and `Chrono_settings_diagnostic` SHALL provide member and free `to_string`, non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization. `Chrono_settings` SHALL provide `to_string`, `to_json`, and a `std::formatter` but no `from_json`, because it is reconstructed by the loader.

#### Scenario: Settings format through std::format

- **WHEN** `std::format("{}", settings)` is evaluated for a loaded `Chrono_settings`
- **THEN** the result equals `settings.to_string()`

### Requirement: Merged settings apply to provider configuration

The system SHALL provide an apply helper that populates a `ChronoProvider_config` from merged settings, transferring the collection interval and the core mask.

#### Scenario: Provider config receives every mapped field

- **WHEN** merged settings set an interval of one second and a core mask of `0x3`
- **THEN** the applied `ChronoProvider_config` carries those values

### Requirement: Settings loading is cold path only

Loading, validation, diagnostics, and config application SHALL execute before provider initialization and SHALL NOT be invoked from a snapshot or producer path. The loader may allocate (`std::string`, `nlohmann::json`, diagnostic storage); steady-state snapshotting SHALL retain its zero-allocation guarantee.

#### Scenario: Snapshot path is untouched

- **WHEN** a provider is initialized from loaded settings and snapshots then run in a measured window
- **THEN** the steady-state zero-allocation guarantee still holds with no allocations attributed to settings
