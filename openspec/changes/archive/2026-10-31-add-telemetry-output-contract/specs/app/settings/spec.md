# Spec Delta

## Purpose

Extends the application settings pipeline so metrics and tracing settings can carry the existing JSONL file-sink configuration, merged with the same precedence and best-effort diagnostics as every provider field, instead of leaving sink paths hard-coded in application policy.

## ADDED Requirements

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
