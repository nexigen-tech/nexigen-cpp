# Spec Delta

## Purpose

Extends chrono settings with the existing JSONL file-sink configuration so a chrono snapshot stream can be redirected through explicit settings instead of a hard-coded path, while keeping the loader's precedence, best-effort diagnostics, and hot-path guarantees unchanged.

## ADDED Requirements

### Requirement: Chrono settings carry a sink configuration

`Chrono_settings` SHALL contain a `sink` member of type `chrono::JsonFileChronoSink_config`. A default-constructed `sink` SHALL have an empty `path` and `append == true`; an empty `path` SHALL mean "no explicit sink configured" and SHALL NOT be a diagnostic. The chrono `sink` object SHALL read `path` and `append`; keys absent from the object SHALL inherit the current value and unknown keys SHALL be ignored.

#### Scenario: Sink loads from the local file

- **WHEN** the local chrono file sets `sink.path` to `chrono.jsonl` and `sink.append` to `false`
- **THEN** the merged `Chrono_settings::sink` carries that path and `append == false`

#### Scenario: Sink loads from the global file

- **WHEN** only the global chrono file sets `sink.path`
- **THEN** the merged `Chrono_settings::sink` carries the global path

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

A `sink` value that is not an object, a non-string `path`, or a non-boolean `append` SHALL be recorded as a `Chrono_settings_diagnostic` naming the source and field. The loader SHALL keep the last valid or default sink value, SHALL continue loading remaining fields, SHALL NOT open, create, or validate the sink path, and SHALL NOT abort or throw.

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

`Chrono_settings` `to_string()` and `to_json` output SHALL include the full `sink` configuration in addition to provider fields, loaded flags, and diagnostics, and its `std::formatter` rendering SHALL equal `to_string()`. `Chrono_settings` SHALL continue to provide `to_json` only and SHALL NOT provide `from_json`; `apply_to` SHALL leave `ChronoProvider_config` untouched by the sink because sink construction is a separate policy concern.

#### Scenario: Chrono sink renders and serializes

- **WHEN** `to_string()`, `std::format("{}", settings)`, and `to_json` are evaluated on a `Chrono_settings` with a configured sink
- **THEN** the text names the sink path and append flag, the formatted text equals `to_string()`, and the JSON contains the same sink fields

#### Scenario: Applying settings ignores the sink

- **WHEN** `apply_to` maps settings with a configured sink into a `ChronoProvider_config`
- **THEN** the provider config receives only the provider fields and no sink data

#### Scenario: Loader output still has no from_json

- **WHEN** the test translation unit checks for `from_json` on `Chrono_settings`
- **THEN** the overload remains absent and a compile-time assertion pins that contract
