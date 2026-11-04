# tracing/tracing-settings Specification

## Purpose

Loads explicitly supplied global and local JSON settings for the tracing provider, merges them with documented precedence, and turns the merged result into a `TracerProviderConfig` plus a file-sink configuration, without touching any hot path.

## Requirements

### Requirement: Settings sources are explicit

The system SHALL read tracing settings only from source paths supplied by the caller: an optional global JSON file and an optional local JSON file. It MUST NOT consult implicit paths, environment-dependent directories, or files discovered by scanning the filesystem.

#### Scenario: Caller supplies both paths

- **WHEN** `load_tracing_settings` is called with existing global and local paths
- **THEN** both files are parsed in that order and the result reports `global_loaded` and `local_loaded` as true

#### Scenario: Missing source is not a failure

- **WHEN** either supplied path does not exist or an empty path is supplied
- **THEN** the load completes with the corresponding loaded flag false, no diagnostic for that source, and the other source or defaults still apply

#### Scenario: Empty paths yield defaults

- **WHEN** both supplied paths are empty
- **THEN** the result carries the default-constructed `TracerProviderConfig` values and reports neither source as loaded

### Requirement: Tracing settings map every provider field

`tracing::Settings` SHALL wrap `tracing::TracerProviderConfig` and read these JSON keys: `core_mask`, `max_tracers`, `max_spans_per_window`, `window_ns`, `worker_poll_interval_us`. Defaults SHALL equal the default-constructed `TracerProviderConfig`.

#### Scenario: Tracing fields load from the global file

- **WHEN** the global tracing file sets a core mask, tracer limit, span budget, and worker poll interval
- **THEN** the merged `tracing::Settings::options` carries those values and `global_loaded` is true

#### Scenario: Rate budget defaults to unlimited

- **WHEN** neither file nor environment sets the span budget or window
- **THEN** both remain zero, which the provider interprets as no rate limiting

### Requirement: Merge precedence matches the other subsystems

Merged tracing settings SHALL follow the precedence built-in defaults, then global file, then local file, then environment overrides, field by field: a field absent from a higher-priority source SHALL inherit the lower-priority value.

#### Scenario: Local overrides global

- **WHEN** the global tracing file sets `max_tracers` to 16 and the local file sets it to 32
- **THEN** the merged value is 32

#### Scenario: Environment overrides files

- **WHEN** a tracing file sets `core_mask` to 1 and `NEXIGEN_TRACING_CORE_MASK` sets it to 4
- **THEN** the merged `core_mask` is 4

### Requirement: Environment overrides are documented and typed

The system SHALL support these overrides with strict parsing: `NEXIGEN_TRACING_CORE_MASK`, `NEXIGEN_TRACING_MAX_TRACERS`, `NEXIGEN_TRACING_MAX_SPANS_PER_WINDOW`, and `NEXIGEN_TRACING_WINDOW_NS`. Unset or empty variables SHALL leave the lower-priority value untouched; other fields are file-only.

#### Scenario: Invalid override is a diagnostic

- **WHEN** `NEXIGEN_TRACING_MAX_TRACERS=many`
- **THEN** the loader records an environment diagnostic, keeps the previous value, and returns `InvalidArgument`

### Requirement: Invalid input degrades to best effort

The loader SHALL treat malformed JSON, wrong types, invalid ranges, and unparsable values as diagnostics while keeping the last valid or default value. A missing file is not a diagnostic. The loader MUST populate its output and MUST NOT abort, throw, or require the caller to stop provider initialization.

#### Scenario: Malformed tracing JSON keeps defaults

- **WHEN** the local tracing file contains invalid JSON
- **THEN** the loader returns a failure `Status`, records a local-source parse diagnostic, and the tracing options remain usable defaults

#### Scenario: Out-of-range value is rejected

- **WHEN** a tracing file sets `max_tracers` above the provider's documented limit
- **THEN** a diagnostic is recorded and the previous/default value is kept

### Requirement: Tracing settings carry a sink member

`tracing::Settings` SHALL contain a `sink` member of type `tracing::JsonFileTraceSinkConfig`. A default-constructed `sink` SHALL have an empty `path`; an empty `path` SHALL mean "no explicit sink configured" and SHALL NOT be a diagnostic.

#### Scenario: Absent sink block leaves an unconfigured sink

- **WHEN** neither tracing source contains a `sink` object
- **THEN** the merged sink keeps an empty `path` and all other sink fields keep their defaults

#### Scenario: Empty path is accepted as unconfigured

- **WHEN** a tracing file sets `sink.path` to an empty string
- **THEN** the loader records no diagnostic and the merged sink remains unconfigured

### Requirement: Sink object reads the file-sink keys

The tracing `sink` object SHALL read `path`, `rotation`, `max_spans_per_window`, and `window_ns`. Keys absent from the `sink` object SHALL inherit the current value, and unknown keys SHALL be ignored.

#### Scenario: Tracing sink loads from the global file

- **WHEN** the global tracing file sets `sink.path`, `sink.rotation` to `Hourly`, and both rate limits
- **THEN** the merged `tracing::Settings::sink` carries the path, `RotationPolicy::Hourly`, and both limits

### Requirement: Sink fields merge with documented precedence

The `sink` block SHALL merge field by field through defaults → global file → local file, exactly like provider fields. The documented environment overrides SHALL NOT modify `sink`; sink configuration SHALL be file-only.

#### Scenario: Local sink path overrides global

- **WHEN** the global tracing file sets `sink.path` to `a.jsonl` and the local file sets it to `b.jsonl`
- **THEN** the merged tracing sink path is `b.jsonl`

#### Scenario: Absent local sink field inherits global

- **WHEN** the global tracing file sets `sink.max_spans_per_window` and the local file sets only `sink.path`
- **THEN** the merged span budget keeps the global value and the path takes the local value

### Requirement: Invalid sink configuration degrades to best effort

A `sink` value that is not an object, a non-string `path`, a non-integer rate limit, or an unknown `rotation` name SHALL be recorded as a settings diagnostic naming the source and field. The loader SHALL keep the last valid or default sink value, SHALL continue loading remaining fields, SHALL NOT open, create, or validate the sink path, and SHALL NOT abort or throw.

#### Scenario: Unknown rotation name keeps the previous policy

- **WHEN** a local tracing file sets `sink.rotation` to `Sometimes`
- **THEN** a local-source diagnostic names `sink.rotation`, the merged rotation keeps its previous value, and loading continues

#### Scenario: Sink path is not touched during load

- **WHEN** a tracing file names a sink path whose directory does not exist
- **THEN** loading succeeds without creating the file and the path is carried verbatim for later resolution

### Requirement: Tracing settings are serializable

`tracing::Settings` SHALL provide member and free `to_string()`, a `std::formatter` specialization, and `to_json`, but SHALL NOT provide `from_json` because it is reconstructed by the loader. `to_string()` and `to_json` output SHALL include the full `sink` configuration in addition to provider fields, loaded flags, and diagnostics, and the `std::formatter` rendering SHALL equal `to_string()`.

#### Scenario: Tracing sink renders and serializes

- **WHEN** `to_string()`, `std::format("{}", settings)`, and `to_json` are evaluated on a `tracing::Settings` with a configured sink
- **THEN** the text names the sink path and rotation, the formatted text equals `to_string()`, and the JSON contains the same sink fields

#### Scenario: Loader output has no from_json

- **WHEN** the test translation unit checks for `from_json` on `tracing::Settings`
- **THEN** the overload is absent and a compile-time assertion pins that contract

### Requirement: Settings loading is cold path only

Loading, validation, diagnostics, and serialization SHALL execute before provider initialization and SHALL NOT be called from a span, snapshot, or sink hot path. The loader may allocate (`std::string`, `nlohmann::json`, diagnostic storage); steady-state tracing SHALL retain its zero-allocation guarantee.

#### Scenario: Span path is untouched

- **WHEN** a provider is initialized from loaded settings and spans then run in a measured window
- **THEN** no settings parsing, JSON access, or allocation occurs in the span path
