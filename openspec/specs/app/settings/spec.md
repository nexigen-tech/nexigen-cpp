# app/settings Specification

## Purpose

Defines the application-level settings pipeline: separate global/local JSON pairs for metrics, tracing, and chrono, merged with the same precedence and best-effort diagnostics as logging, and aggregated with the existing logging settings into one `App_settings` value consumed by `App::initialize`.

## Requirements

### Requirement: Metrics and tracing use explicit file pairs

The system SHALL load metrics settings and tracing settings only from caller-supplied `Settings_paths` (a global file and a local file each; an empty path disables that source). It MUST NOT discover configuration implicitly. The logging pair remains the existing `logging::load_logging_settings` source.

#### Scenario: Three independent pairs are supplied

- **WHEN** the application supplies logging, metrics, and tracing path pairs
- **THEN** each subsystem reads its own pair and reports its own loaded flags

#### Scenario: Missing subsystem files are not failures

- **WHEN** any supplied metrics or tracing path is absent or empty
- **THEN** that subsystem reports the source as not loaded, records no diagnostic for it, and keeps the built-in defaults

### Requirement: Chrono settings use an explicit file pair

The system SHALL load chrono settings only from a caller-supplied `Chrono_settings_paths` (a global file and a local file; an empty path disables that source), reusing the chrono module's `load_chrono_settings`. It MUST NOT discover configuration implicitly.

#### Scenario: Chrono pair is supplied

- **WHEN** the application supplies a chrono path pair
- **THEN** chrono reads its own pair and reports its own `global_loaded`/`local_loaded` flags

#### Scenario: Missing chrono files are not failures

- **WHEN** any supplied chrono path is absent or empty
- **THEN** chrono reports that source as not loaded, records no diagnostic for it, and keeps the built-in defaults

### Requirement: Metrics settings map every provider field

`Metrics_settings` SHALL wrap `metrics::MetricProvider_config` and read these JSON keys: `collect_interval_ns`, `core_mask`, `max_meters`, `max_counters`, `max_gauges`, `max_statistics`, `max_explicit_histograms`, `max_exponential_histograms`. Defaults SHALL equal the default-constructed `MetricProvider_config`.

#### Scenario: Metrics fields load from the local file

- **WHEN** the local metrics file sets a collection interval and every capacity
- **THEN** the merged `Metrics_settings::options` carries those values and `local_loaded` is true

#### Scenario: Defaults survive an absent file

- **WHEN** no metrics file sets a field
- **THEN** the corresponding `MetricProvider_config` default is preserved

### Requirement: Tracing settings map every provider field

`Tracing_settings` SHALL wrap `tracing::TracerProvider_config` and read these JSON keys: `core_mask`, `max_tracers`, `max_spans_per_window`, `window_ns`. Defaults SHALL equal the default-constructed `TracerProvider_config`.

#### Scenario: Tracing fields load from the global file

- **WHEN** the global tracing file sets a core mask, tracer limit, and span budget
- **THEN** the merged `Tracing_settings::options` carries those values and `global_loaded` is true

#### Scenario: Rate budget defaults to unlimited

- **WHEN** neither file nor environment sets the span budget or window
- **THEN** both remain zero, which the provider interprets as no rate limiting

### Requirement: Merge precedence matches logging

Each subsystem SHALL merge defaults → global file → local file → environment overrides, field by field: a field absent from a higher-priority source inherits the lower-priority value. The app settings loader SHALL follow the same rule for all three subsystems.

#### Scenario: Local overrides global for metrics

- **WHEN** the global metrics file sets `max_counters` to 64 and the local file sets it to 128
- **THEN** the merged value is 128

#### Scenario: Environment overrides files

- **WHEN** a tracing file sets `core_mask` to 1 and `NEXIGEN_TRACING_CORE_MASK` sets it to 4
- **THEN** the merged `core_mask` is 4

### Requirement: Environment overrides are documented and typed

The system SHALL support these overrides with strict parsing: `NEXIGEN_METRICS_COLLECT_INTERVAL_NS`, `NEXIGEN_METRICS_CORE_MASK`, `NEXIGEN_TRACING_CORE_MASK`, `NEXIGEN_TRACING_MAX_TRACERS`, `NEXIGEN_TRACING_MAX_SPANS_PER_WINDOW`, and `NEXIGEN_TRACING_WINDOW_NS`. Unset or empty variables SHALL leave the lower-priority value untouched; other fields are file-only.

#### Scenario: Interval override parses nanoseconds

- **WHEN** `NEXIGEN_METRICS_COLLECT_INTERVAL_NS=50000000`
- **THEN** the merged metrics interval is 50 ms and `0` remains the documented "harvester disabled" value

#### Scenario: Invalid override is a diagnostic

- **WHEN** `NEXIGEN_TRACING_MAX_TRACERS=many`
- **THEN** the loader records an environment diagnostic, keeps the previous value, and returns `InvalidArgument`

### Requirement: Invalid input degrades to best effort

Each loader SHALL treat malformed JSON, wrong types, invalid ranges, and unparsable values as diagnostics while keeping the last valid or default value. A missing file is not a diagnostic. The loader MUST populate its output and MUST NOT abort, throw, or require the caller to stop initialization.

#### Scenario: Malformed metrics JSON keeps defaults

- **WHEN** the local metrics file contains invalid JSON
- **THEN** the loader returns a failure `Status`, records a local-source parse diagnostic, and the metrics options remain usable defaults

#### Scenario: Out-of-range value is rejected

- **WHEN** a tracing file sets `max_tracers` above the provider's documented limit
- **THEN** a diagnostic is recorded and the previous/default value is kept

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

### Requirement: Settings loading is cold path only

App settings loading, validation, diagnostics, and serialization SHALL execute before `App::initialize` (or inside it before workers start) and SHALL NOT be called from a tick, producer, or worker hot path. Steady-state ticking SHALL retain zero allocation and no locking.

#### Scenario: Tick path stays allocation-free

- **WHEN** an application runs ticks after settings were loaded
- **THEN** the tick loop performs no settings parsing, JSON access, or allocation

### Requirement: Metrics and tracing settings carry a sink member

`Metrics_settings` SHALL contain a `sink` member of type `metrics::JsonFileMetricSink_config` and `Tracing_settings` SHALL contain a `sink` member of type `tracing::JsonFileTraceSink_config`. A default-constructed `sink` SHALL have an empty `path`; an empty `path` SHALL mean "no explicit sink configured" and SHALL NOT be a diagnostic.

#### Scenario: Absent sink block leaves an unconfigured sink

- **WHEN** neither metrics nor tracing source contains a `sink` object
- **THEN** both merged sinks keep an empty `path` and all other sink fields keep their defaults

#### Scenario: Empty path is accepted as unconfigured

- **WHEN** a settings file sets `sink.path` to an empty string
- **THEN** the loader records no diagnostic and the merged sink remains unconfigured

### Requirement: Sink objects read the existing file-sink keys

The metrics `sink` object SHALL read `path` and `append`; the tracing `sink` object SHALL read `path`, `rotation`, `max_spans_per_window`, and `window_ns`. Keys absent from a `sink` object SHALL inherit the current value, and unknown keys SHALL be ignored.

#### Scenario: Metrics sink loads from the local file

- **WHEN** the local metrics file sets `sink.path` to `metrics.jsonl` and `sink.append` to `false`
- **THEN** the merged `Metrics_settings::sink` carries that path and `append == false`

#### Scenario: Tracing sink loads from the global file

- **WHEN** the global tracing file sets `sink.path`, `sink.rotation` to `Hourly`, and both rate limits
- **THEN** the merged `Tracing_settings::sink` carries the path, `RotationPolicy::Hourly`, and both limits

### Requirement: Sink fields merge with documented precedence

The `sink` block SHALL merge field by field through defaults → global file → local file, exactly like provider fields: a field present in a higher-priority source SHALL override the lower-priority value and an absent field SHALL inherit it. The documented environment overrides for provider fields SHALL NOT modify `sink`; sink configuration SHALL be file-only, and instance-directory fallback for sink paths SHALL be handled by `app/telemetry-layout`, not by settings.

#### Scenario: Local sink path overrides global

- **WHEN** the global metrics file sets `sink.path` to `a.jsonl` and the local file sets it to `b.jsonl`
- **THEN** the merged metrics sink path is `b.jsonl`

#### Scenario: Absent local sink field inherits global

- **WHEN** the global tracing file sets `sink.max_spans_per_window` and the local file sets only `sink.path`
- **THEN** the merged span budget keeps the global value and the path takes the local value

#### Scenario: Environment provider overrides do not touch the sink

- **WHEN** a metrics file sets `sink.path` and `NEXIGEN_METRICS_COLLECT_INTERVAL_NS` is also set
- **THEN** the interval override applies to `options` and the sink path is unchanged

### Requirement: Invalid sink configuration degrades to best effort

A `sink` value that is not an object, a non-string `path`, a non-boolean `append`, a non-integer rate limit, or an unknown `rotation` name SHALL be recorded as a settings diagnostic naming the source and field. The loader SHALL keep the last valid or default sink value, SHALL continue loading remaining fields, SHALL NOT open, create, or validate the sink path, and SHALL NOT abort or throw.

#### Scenario: Unknown rotation name keeps the previous policy

- **WHEN** a local tracing file sets `sink.rotation` to `Sometimes`
- **THEN** a local-source diagnostic names `sink.rotation`, the merged rotation keeps its previous value, and loading continues

#### Scenario: Non-object sink keeps the previous sink

- **WHEN** a metrics file sets `sink` to a string or array
- **THEN** a diagnostic names `sink`, the entire previous sink configuration is kept, and the loader returns a failure `Status` while still populating provider fields

#### Scenario: Sink path is not touched during load

- **WHEN** a settings file names a sink path whose directory does not exist
- **THEN** loading succeeds without creating the file and the path is carried verbatim for later resolution

### Requirement: Sink configuration is part of settings serialization

`Metrics_settings` and `Tracing_settings` `to_string()` and `to_json` output SHALL include the full `sink` configuration in addition to provider fields, loaded flags, and diagnostics, and their `std::formatter` rendering SHALL equal `to_string()`. Loader-produced settings SHALL continue to provide `to_json` only and SHALL NOT provide `from_json`; `App_settings` serialization SHALL include both sink configurations through its members.

#### Scenario: Metrics sink renders and serializes

- **WHEN** `to_string()`, `std::format("{}", settings)`, and `to_json` are evaluated on a `Metrics_settings` with a configured sink
- **THEN** the text names the sink path and append flag, the formatted text equals `to_string()`, and the JSON contains the same sink fields

#### Scenario: Tracing sink appears in App_settings JSON

- **WHEN** `to_json` is evaluated on an `App_settings` whose tracing sink sets a path and rotation
- **THEN** the resulting JSON contains the tracing sink path and rotation

#### Scenario: Loader output still has no from_json

- **WHEN** the test translation unit checks for `from_json` on `Metrics_settings` or `Tracing_settings`
- **THEN** the overload remains absent and a compile-time assertion pins that contract
