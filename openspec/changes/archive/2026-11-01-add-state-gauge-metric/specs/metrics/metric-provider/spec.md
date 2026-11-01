# Spec Delta

## MODIFIED Requirements

### Requirement: Factory hierarchy and passkey construction

Metrics SHALL be created exclusively through a `Meter` obtained from `MetricProvider::get_meter(name, version)`. Instrument constructors SHALL be guarded by a private `MeterPasskey` and `friend class Meter`; user code SHALL NOT be able to construct `Counter`, `Gauge`, `DescriptiveStatistics`, `ExplicitBucketHistogram`, `ExponentialHistogram`, or `StateGauge` directly.

#### Scenario: Direct construction is rejected

- **WHEN** `std::is_constructible_v` is evaluated for each instrument type and for `MeterPasskey` with every publicly reachable argument combination
- **THEN** every result is `false`, and a scratch translation unit attempting direct construction fails to compile

#### Scenario: Meter is the only instrument factory

- **WHEN** user code needs a counter, gauge, statistics, histogram, or state
- **THEN** it must call the corresponding `Meter` creation method, which is the only API returning the instrument

#### Scenario: Meter creation goes through the provider

- **WHEN** user code attempts to construct a `Meter` directly
- **THEN** construction is unavailable at compile time; the only source is `MetricProvider::get_meter`

#### Scenario: Meter identity is retained

- **WHEN** `get_meter("engine", "1.2.0")` succeeds and an instrument is created through that meter
- **THEN** the harvested record reports the meter name `"engine"` and version `"1.2.0"`

#### Scenario: Passkey tag carries the diagnostic contract

- **WHEN** the `MeterPasskey` guard type's surface is inspected
- **THEN** it provides `to_string` and `std::formatter` rendering `"MeterPasskey"` and exposes no `from_json`, preserving the Meter-only creation rule

### Requirement: Meter instrument creation

`Meter` SHALL expose creation methods for counters, gauges, descriptive statistics, explicit-bucket histograms, exponential histograms, and state gauges, each accepting the instrument name and the policy template parameters and creation arguments that determine its behavior. Every creation call SHALL return `core::Result` and never throw.

#### Scenario: Counter and gauge creation

- **WHEN** a meter creates a counter and a gauge with valid names and policies
- **THEN** both calls succeed, the instruments record values, and a later harvest reports each with the matching `MetricKind`

#### Scenario: State gauge creation

- **WHEN** a meter creates a state gauge with a valid name and policies
- **THEN** the call succeeds, the instrument records a string value, and a later harvest reports it with `MetricKind::State` and the matching value

#### Scenario: Histogram creation parameters

- **WHEN** a meter creates an explicit histogram with strictly increasing finite bounds or an exponential histogram with a valid scale
- **THEN** creation succeeds and the stored configuration is reflected in the snapshot

#### Scenario: Invalid histogram parameters

- **WHEN** explicit bounds are empty, unsorted, duplicated, non-finite, or exceed the inline capacity, or the exponential scale is outside its supported range
- **THEN** the call returns `core::ErrorCode::InvalidArgument` and no instrument is registered

#### Scenario: Instrument types are distinct

- **WHEN** the same name is used for two different instrument kinds under one meter
- **THEN** the second creation returns `core::ErrorCode::InvalidArgument`

### Requirement: Record and batch snapshot serialization

`MetricRecord` and `MetricBatchSnapshot` SHALL each implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter` support, so harvested batches round-trip through JSON diagnostics.

#### Scenario: Record JSON round-trip

- **WHEN** a `MetricRecord` for a histogram carrying bounds, bucket counts, and percentiles is serialized and parsed back
- **THEN** every scalar, name, policy, bound, bucket count, and percentile survives the round-trip

#### Scenario: State record JSON round-trip

- **WHEN** a `MetricRecord` for a state gauge carrying a string value is serialized and parsed back
- **THEN** the string value and the `MetricKind::State` classification survive the round-trip

#### Scenario: Batch JSON round-trip

- **WHEN** a `MetricBatchSnapshot` containing multiple records is serialized and parsed back
- **THEN** the record count, collection timestamp, and per-record contents survive the round-trip

#### Scenario: Snapshot diagnostics render

- **WHEN** `to_string()` is called on a record and a batch, and the free functions are called on the same objects
- **THEN** each member/free pair returns equal, non-empty strings, and `std::format` renders the same text
