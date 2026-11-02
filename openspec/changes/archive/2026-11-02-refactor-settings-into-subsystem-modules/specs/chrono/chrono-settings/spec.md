# Spec Delta

## MODIFIED Requirements

### Requirement: Settings diagnostics are serializable

`chrono::Settings` SHALL provide member and free `to_string`, a `std::formatter` specialization, and `to_json`, but no `from_json`, because it is reconstructed by the loader. The shared `SettingsPaths` and `SettingsDiagnostic` serialization contract SHALL be provided by `core/settings-vocabulary`.

#### Scenario: Settings format through std::format

- **WHEN** `std::format("{}", settings)` is evaluated for a loaded `chrono::Settings`
- **THEN** the result equals `settings.to_string()`

### Requirement: Merged settings apply to provider configuration

The system SHALL provide an apply helper that populates a `ChronoProviderConfig` from merged settings, transferring the collection interval and the core mask.

#### Scenario: Provider config receives every mapped field

- **WHEN** merged settings set an interval of one second and a core mask of `0x3`
- **THEN** the applied `ChronoProviderConfig` carries those values

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
