# metrics/metrics-settings Specification

## Purpose

Loads explicitly supplied global and local JSON settings for the metrics provider, merges them with documented precedence, and turns the merged result into a `MetricProviderConfig` plus a file-sink configuration, without touching any hot path.

## Requirements

### Requirement: Settings sources are explicit

The system SHALL read metrics settings only from source paths supplied by the caller: an optional global JSON file and an optional local JSON file. It MUST NOT consult implicit paths, environment-dependent directories, or files discovered by scanning the filesystem.

#### Scenario: Caller supplies both paths

- **WHEN** `load_metrics_settings` is called with existing global and local paths
- **THEN** both files are parsed in that order and the result reports `global_loaded` and `local_loaded` as true

#### Scenario: Missing source is not a failure

- **WHEN** either supplied path does not exist or an empty path is supplied
- **THEN** the load completes with the corresponding loaded flag false, no diagnostic for that source, and the other source or defaults still apply

#### Scenario: Empty paths yield defaults

- **WHEN** both supplied paths are empty
- **THEN** the result carries the default-constructed `MetricProviderConfig` values and reports neither source as loaded

### Requirement: Metrics settings map every provider field

`metrics::Settings` SHALL wrap `metrics::MetricProviderConfig` and read these JSON keys: `collect_interval_ns`, `worker_thread`, `max_meters`, `max_counters`, `max_gauges`, `max_statistics`, `max_explicit_histograms`, `max_exponential_histograms`, plus legacy `core_mask` as a compatibility input. Defaults SHALL equal the default-constructed `MetricProviderConfig`.

#### Scenario: Metrics fields load from the local file

- **WHEN** the local metrics file sets a collection interval, every capacity, and a complete worker thread spec
- **THEN** the merged `metrics::Settings::options` carries those values and `local_loaded` is true

#### Scenario: Defaults survive an absent file

- **WHEN** no metrics file sets a field
- **THEN** the corresponding `MetricProviderConfig` default is preserved, including inherited best-effort worker attributes

### Requirement: Merge precedence matches the other subsystems

Merged metrics settings SHALL follow the precedence built-in defaults, then global file, then local file, then environment overrides, field by field: a field absent from a higher-priority source SHALL inherit the lower-priority value.

#### Scenario: Local overrides global

- **WHEN** the global metrics file sets `max_counters` to 64 and the local file sets it to 128
- **THEN** the merged value is 128

#### Scenario: Environment overrides files

- **WHEN** a metrics file sets `core_mask` to 1 and `NEXIGEN_METRICS_CORE_MASK` sets it to 4
- **THEN** the merged `core_mask` is 4

### Requirement: Environment overrides are documented and typed

The system SHALL support these overrides with strict parsing: `NEXIGEN_METRICS_COLLECT_INTERVAL_NS` and `NEXIGEN_METRICS_CORE_MASK`. Unset or empty variables SHALL leave the lower-priority value untouched; other fields are file-only.

#### Scenario: Interval override parses nanoseconds

- **WHEN** `NEXIGEN_METRICS_COLLECT_INTERVAL_NS=50000000`
- **THEN** the merged metrics interval is 50 ms and `0` remains the documented "harvester disabled" value

#### Scenario: Invalid override is a diagnostic

- **WHEN** `NEXIGEN_METRICS_CORE_MASK=many`
- **THEN** the loader records an environment diagnostic, keeps the previous value, and returns `InvalidArgument`

### Requirement: Invalid input degrades to best effort

The loader SHALL treat malformed JSON, wrong types, invalid ranges, and unparsable values as diagnostics while keeping the last valid or default value. A missing file is not a diagnostic. The loader MUST populate its output and MUST NOT abort, throw, or require the caller to stop provider initialization.

#### Scenario: Malformed metrics JSON keeps defaults

- **WHEN** the local metrics file contains invalid JSON
- **THEN** the loader returns a failure `Status`, records a local-source parse diagnostic, and the metrics options remain usable defaults

#### Scenario: Out-of-range value is rejected

- **WHEN** a metrics file sets a capacity above the provider's documented limit
- **THEN** a diagnostic is recorded and the previous/default value is kept

### Requirement: Metrics settings carry a sink member

`metrics::Settings` SHALL contain a `sink` member of type `metrics::JsonFileMetricSinkConfig`. A default-constructed `sink` SHALL have an empty `path`; an empty `path` SHALL mean "no explicit sink configured" and SHALL NOT be a diagnostic.

#### Scenario: Absent sink block leaves an unconfigured sink

- **WHEN** neither metrics source contains a `sink` object
- **THEN** the merged sink keeps an empty `path` and all other sink fields keep their defaults

#### Scenario: Empty path is accepted as unconfigured

- **WHEN** a metrics file sets `sink.path` to an empty string
- **THEN** the loader records no diagnostic and the merged sink remains unconfigured

### Requirement: Sink object reads the file-sink keys

The metrics `sink` object SHALL read `path` and `append`. Keys absent from the `sink` object SHALL inherit the current value, and unknown keys SHALL be ignored.

#### Scenario: Metrics sink loads from the local file

- **WHEN** the local metrics file sets `sink.path` to `metrics.jsonl` and `sink.append` to `false`
- **THEN** the merged `metrics::Settings::sink` carries that path and `append == false`

### Requirement: Sink fields merge with documented precedence

The `sink` block SHALL merge field by field through defaults → global file → local file, exactly like provider fields. The documented environment overrides SHALL NOT modify `sink`; sink configuration SHALL be file-only.

#### Scenario: Local sink path overrides global

- **WHEN** the global metrics file sets `sink.path` to `a.jsonl` and the local file sets it to `b.jsonl`
- **THEN** the merged metrics sink path is `b.jsonl`

#### Scenario: Environment provider overrides do not touch the sink

- **WHEN** a metrics file sets `sink.path` and `NEXIGEN_METRICS_COLLECT_INTERVAL_NS` is also set
- **THEN** the interval override applies to `options` and the sink path is unchanged

### Requirement: Invalid sink configuration degrades to best effort

A `sink` value that is not an object, a non-string `path`, or a non-boolean `append` SHALL be recorded as a settings diagnostic naming the source and field. The loader SHALL keep the last valid or default sink value, SHALL continue loading remaining fields, SHALL NOT open, create, or validate the sink path, and SHALL NOT abort or throw.

#### Scenario: Non-object sink keeps the previous sink

- **WHEN** a metrics file sets `sink` to a string or array
- **THEN** a diagnostic names `sink`, the entire previous sink configuration is kept, and the loader returns a failure `Status` while still populating provider fields

#### Scenario: Sink path is not touched during load

- **WHEN** a metrics file names a sink path whose directory does not exist
- **THEN** loading succeeds without creating the file and the path is carried verbatim for later resolution

### Requirement: Metrics settings are serializable

`metrics::Settings` SHALL provide member and free `to_string()`, a `std::formatter` specialization, and `to_json`, but SHALL NOT provide `from_json` because it is reconstructed by the loader. `to_string()` and `to_json` output SHALL include the full `sink` configuration in addition to provider fields, loaded flags, and diagnostics, and the `std::formatter` rendering SHALL equal `to_string()`.

#### Scenario: Metrics sink renders and serializes

- **WHEN** `to_string()`, `std::format("{}", settings)`, and `to_json` are evaluated on a `metrics::Settings` with a configured sink
- **THEN** the text names the sink path and append flag, the formatted text equals `to_string()`, and the JSON contains the same sink fields

#### Scenario: Loader output has no from_json

- **WHEN** the test translation unit checks for `from_json` on `metrics::Settings`
- **THEN** the overload is absent and a compile-time assertion pins that contract

### Requirement: Settings loading is cold path only

Loading, validation, diagnostics, and serialization SHALL execute before provider initialization and SHALL NOT be called from a collection, snapshot, or sink hot path. The loader may allocate (`std::string`, `nlohmann::json`, diagnostic storage); steady-state collection SHALL retain its zero-allocation guarantee.

#### Scenario: Collection path is untouched

- **WHEN** a provider is initialized from loaded settings and collections then run in a measured window
- **THEN** no settings parsing, JSON access, or allocation occurs in the collection loop

### Requirement: Worker thread settings preserve legacy affinity compatibility

The metrics loader SHALL read canonical `worker_thread` fields and normalize legacy `core_mask` and `NEXIGEN_METRICS_CORE_MASK` into worker CPUs. Canonical fields SHALL win within one source, while normal global → local → environment precedence SHALL remain. The canonical thread object SHALL be file-only, and an explicitly empty canonical CPU array SHALL clear a lower-priority pin.

#### Scenario: Canonical worker fields merge across sources

- **WHEN** global settings provide legacy affinity and local settings provide a named FIFO worker on CPU 70
- **THEN** the merged options contain the local name/policy/priority and CPU 70

#### Scenario: Legacy environment override remains effective

- **WHEN** no higher-priority canonical setting exists and `NEXIGEN_METRICS_CORE_MASK=5`
- **THEN** the merged worker CPU set equals the CPUs represented by mask 5 and the apply mode remains best effort

#### Scenario: Empty canonical CPUs clear inherited affinity

- **WHEN** a local file explicitly sets `worker_thread.cpus` to `[]` after a global pin
- **THEN** the merged worker has no affinity request
