# Spec Delta

## ADDED Requirements

### Requirement: Provider kind setting

`chrono::Settings` and the provider configuration SHALL carry a `kind` field of the new public enum `chrono::ProviderKind` with enumerators `daemon` and `manual` (uint8_t, strict order). The kind SHALL merge file-only through the documented defaults → global → local precedence, and no documented `NEXIGEN_CHRONO_*` environment override SHALL target it. An unparsable kind value SHALL be recorded as a diagnostic and keep the previous merged value.

#### Scenario: Default kind is daemon

- **WHEN** neither settings source sets a kind
- **THEN** the merged kind is `daemon` and no diagnostic is recorded

#### Scenario: Local file overrides global kind

- **WHEN** the global file sets `kind` to `daemon` and the local file sets `kind` to `manual`
- **THEN** the merged kind is `manual`

#### Scenario: Invalid kind keeps the previous value

- **WHEN** a file sets `kind` to an unparsable value after a valid source set `daemon`
- **THEN** a diagnostic names the source and field and the merged kind stays `daemon`

#### Scenario: Environment overrides do not touch the kind

- **WHEN** files set `kind` and any documented `NEXIGEN_CHRONO_*` environment variable is set
- **THEN** the merged kind equals the file value and no kind diagnostic is recorded

### Requirement: Manual kind records ignored worker fields

When the merged kind is `manual` while worker attributes are configured, the loader SHALL record a diagnostic naming the provider fields it ignores, and merged worker values SHALL still be stored for diagnostics.

#### Scenario: Manual kind records ignored worker fields

- **WHEN** the merged kind is `manual` and worker attributes (CPU set, scheduling, or name) are configured
- **THEN** a diagnostic names the provider fields as ignored and the merged settings still carry those values

### Requirement: ProviderKind serializes through the standard conversion set

`ProviderKind` SHALL provide the project conversion set: constexpr `to_string`/`from_string` over canonical names, non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization. `chrono::Settings` `to_string()` and `to_json` output SHALL include the merged kind, and its `std::formatter` rendering SHALL equal `to_string()`.

#### Scenario: ProviderKind serializes through the standard conversion set

- **WHEN** each `ProviderKind` value passes through `to_string`/`from_string`, `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::format`
- **THEN** both canonical names round-trip, a garbage-cast value renders `Unknown` and is rejected by `from_string`, a JSON non-string value or unknown name leaves the target unchanged, and the formatter renders the canonical name

#### Scenario: Kind is part of chrono settings serialization

- **WHEN** `to_string()`, `std::format("{}", settings)`, and `to_json` are evaluated on loaded chrono settings
- **THEN** the output names the merged kind and the formatted text equals `to_string()`

## MODIFIED Requirements

### Requirement: Merged settings apply to provider configuration

The system SHALL provide an apply helper that populates a `ProviderConfig` from merged settings, transferring the collection interval, the complete worker thread spec, and the provider kind.

#### Scenario: Provider config receives every mapped field

- **WHEN** merged settings set an interval of one second and a named RR worker on CPUs `{2, 64}` with priority 40
- **THEN** the applied `ProviderConfig` carries the interval and every worker attribute

#### Scenario: Legacy affinity maps to the worker CPU set

- **WHEN** merged settings contain only a legacy core mask of `0x3`
- **THEN** the applied worker CPU set contains CPUs 0 and 1 and the apply mode remains best effort

#### Scenario: Kind is transferred into the provider config

- **WHEN** merged settings carry kind `manual`
- **THEN** the applied `ProviderConfig` carries kind `manual` and re-serialization of the applied config round-trips both the kind and the worker fields

### Requirement: Sink configuration is part of chrono settings serialization

`chrono::Settings` `to_string()` and `to_json` output SHALL include the full `sink` configuration in addition to provider fields, loaded flags, and diagnostics, and its `std::formatter` rendering SHALL equal `to_string()`. `chrono::Settings` SHALL continue to provide `to_json` only and SHALL NOT provide `from_json`; `apply_to` SHALL leave the sink configuration out of `ProviderConfig` because sink construction is a separate policy concern.

#### Scenario: Chrono sink renders and serializes

- **WHEN** `to_string()`, `std::format("{}", settings)`, and `to_json` are evaluated on a `chrono::Settings` with a configured sink
- **THEN** the text names the sink path and append flag, the formatted text equals `to_string()`, and the JSON contains the same sink fields

#### Scenario: Applying settings ignores the sink

- **WHEN** `apply_to` maps settings with a configured sink into a `ProviderConfig`
- **THEN** the provider config receives only the provider fields and no sink data

#### Scenario: Loader output still has no from_json

- **WHEN** the test translation unit checks for `from_json` on `chrono::Settings`
- **THEN** the overload remains absent and a compile-time assertion pins that contract
