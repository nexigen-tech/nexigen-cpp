# chrono/chrono-settings Specification

## Purpose

Loads explicitly supplied global and local JSON settings for the chrono provider, merges them with documented precedence, and turns the merged result into a `ChronoProviderConfig` without touching any hot path.

## Requirements

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

`chrono::Settings` SHALL provide member and free `to_string`, a `std::formatter` specialization, and `to_json`, but no `from_json`, because it is reconstructed by the loader. The shared `SettingsPaths` and `SettingsDiagnostic` serialization contract SHALL be provided by `core/settings-vocabulary`.

#### Scenario: Settings format through std::format

- **WHEN** `std::format("{}", settings)` is evaluated for a loaded `chrono::Settings`
- **THEN** the result equals `settings.to_string()`

### Requirement: Merged settings apply to provider configuration

The system SHALL provide an apply helper that populates a `ChronoProviderConfig` from merged settings, transferring the collection interval and the complete worker thread spec.

#### Scenario: Provider config receives every mapped field

- **WHEN** merged settings set an interval of one second and a named RR worker on CPUs `{2, 64}` with priority 40
- **THEN** the applied `ChronoProviderConfig` carries the interval and every worker attribute

#### Scenario: Legacy affinity maps to the worker CPU set

- **WHEN** merged settings contain only a legacy core mask of `0x3`
- **THEN** the applied worker CPU set contains CPUs 0 and 1 and the apply mode remains best effort

### Requirement: Settings loading is cold path only

Loading, validation, diagnostics, and config application SHALL execute before provider initialization and SHALL NOT be invoked from a snapshot or producer path. The loader may allocate (`std::string`, `nlohmann::json`, diagnostic storage); steady-state snapshotting SHALL retain its zero-allocation guarantee.

#### Scenario: Snapshot path is untouched

- **WHEN** a provider is initialized from loaded settings and snapshots then run in a measured window
- **THEN** the steady-state zero-allocation guarantee still holds with no allocations attributed to settings

### Requirement: Chrono settings carry a sink configuration

`chrono::Settings` SHALL contain a `sink` member of type `chrono::JsonFileChronoSinkConfig`. A default-constructed `sink` SHALL have an empty `path` and `append == true`; an empty `path` SHALL mean "no explicit sink configured" and SHALL NOT be a diagnostic. The chrono `sink` object SHALL read `path` and `append`; keys absent from the object SHALL inherit the current value and unknown keys SHALL be ignored.

#### Scenario: Sink loads from the local file

- **WHEN** the local chrono file sets `sink.path` to `chrono.jsonl` and `sink.append` to `false`
- **THEN** the merged `chrono::Settings::sink` carries that path and `append == false`

#### Scenario: Sink loads from the global file

- **WHEN** only the global chrono file sets `sink.path`
- **THEN** the merged `chrono::Settings::sink` carries the global path

#### Scenario: Absent sink block leaves an unconfigured sink

- **WHEN** neither chrono source contains a `sink` object
- **THEN** the merged sink keeps an empty path and default append behavior

#### Scenario: Empty path is accepted as unconfigured

- **WHEN** a chrono file sets `sink.path` to an empty string
- **THEN** the loader records no diagnostic and the sink remains unconfigured

### Requirement: Sink fields merge with documented precedence

The `sink` block SHALL merge field by field through defaults → global file → local file, exactly like provider fields. The documented `NEXIGEN_CHRONO_*` environment overrides SHALL NOT modify `sink`; sink configuration SHALL be file-only, and instance-directory fallback for sink paths SHALL be handled by `app/telemetry-layout`, not by chrono settings.

#### Scenario: Local sink path overrides global

- **WHEN** the global chrono file sets `sink.path` to `a.jsonl` and the local file sets it to `b.jsonl`
- **THEN** the merged chrono sink path is `b.jsonl`

#### Scenario: Absent local sink field inherits global

- **WHEN** the global chrono file sets `sink.append` to `false` and the local file sets only `sink.path`
- **THEN** the merged append flag stays `false` and the path takes the local value

#### Scenario: Environment overrides do not touch the sink

- **WHEN** a chrono file sets `sink.path` and `NEXIGEN_CHRONO_COLLECT_INTERVAL_NS` is also set
- **THEN** the interval override applies to `options` and the sink path is unchanged

### Requirement: Invalid sink configuration degrades to best effort

A `sink` value that is not an object, a non-string `path`, or a non-boolean `append` SHALL be recorded as a `core::SettingsDiagnostic` naming the source and field. The loader SHALL keep the last valid or default sink value, SHALL continue loading remaining fields, SHALL NOT open, create, or validate the sink path, and SHALL NOT abort or throw.

#### Scenario: Non-object sink keeps the previous sink

- **WHEN** a chrono file sets `sink` to a number or array
- **THEN** a diagnostic names `sink`, the entire previous sink configuration is kept, and the loader still populates merged provider fields

#### Scenario: Wrong path type keeps the previous path

- **WHEN** a local chrono file sets `sink.path` to a number after the global file set a valid path
- **THEN** a local-source diagnostic names `sink.path` and the merged path keeps the global value

#### Scenario: Sink path is not touched during load

- **WHEN** a settings file names a sink path whose directory does not exist
- **THEN** loading succeeds without creating the file and the path is carried verbatim for later resolution

### Requirement: Sink configuration is part of chrono settings serialization

`chrono::Settings` `to_string()` and `to_json` output SHALL include the full `sink` configuration in addition to provider fields, loaded flags, and diagnostics, and its `std::formatter` rendering SHALL equal `to_string()`. `chrono::Settings` SHALL continue to provide `to_json` only and SHALL NOT provide `from_json`; `apply_to` SHALL leave `ChronoProviderConfig` untouched by the sink because sink construction is a separate policy concern.

#### Scenario: Chrono sink renders and serializes

- **WHEN** `to_string()`, `std::format("{}", settings)`, and `to_json` are evaluated on a `chrono::Settings` with a configured sink
- **THEN** the text names the sink path and append flag, the formatted text equals `to_string()`, and the JSON contains the same sink fields

#### Scenario: Applying settings ignores the sink

- **WHEN** `apply_to` maps settings with a configured sink into a `ChronoProviderConfig`
- **THEN** the provider config receives only the provider fields and no sink data

#### Scenario: Loader output still has no from_json

- **WHEN** the test translation unit checks for `from_json` on `chrono::Settings`
- **THEN** the overload remains absent and a compile-time assertion pins that contract

### Requirement: Worker thread settings preserve legacy affinity compatibility

The chrono loader SHALL read canonical `worker_thread` fields and normalize legacy `core_mask` and `NEXIGEN_CHRONO_CORE_MASK` into worker CPUs. Canonical fields SHALL win within one source, normal global → local → environment precedence SHALL remain, and the canonical thread object SHALL be file-only. Explicitly empty canonical CPUs SHALL clear a lower-priority pin.

#### Scenario: Canonical worker fields merge across sources

- **WHEN** global settings provide legacy affinity and local settings provide a named FIFO worker on CPU 64
- **THEN** the merged options contain the local name/policy/priority and CPU 64

#### Scenario: Legacy environment override remains effective

- **WHEN** no higher-priority canonical setting exists and `NEXIGEN_CHRONO_CORE_MASK=5`
- **THEN** the merged worker CPU set equals the CPUs represented by mask 5 and the apply mode remains best effort
