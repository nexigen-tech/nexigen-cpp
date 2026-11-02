# Spec Delta

## MODIFIED Requirements

### Requirement: Meter instrument creation

`Meter` SHALL expose creation methods for counters, gauges, descriptive statistics, explicit-bucket histograms, exponential histograms, and state gauges, each accepting the instrument name, an optional unit string, and the policy template parameters and creation arguments that determine its behavior. Every creation call SHALL return `core::Result` and never throw.

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

#### Scenario: Unit is declared and stored

- **WHEN** a meter creates an instrument with a non-empty unit string
- **THEN** a later harvest reports that instrument with the same unit, and an instrument created without a unit reports an empty unit

### Requirement: Record and batch snapshot serialization

`MetricRecord` and `MetricBatchSnapshot` SHALL each implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter` support, so harvested batches round-trip through JSON diagnostics.

#### Scenario: Record JSON round-trip

- **WHEN** a `MetricRecord` for a histogram carrying bounds, bucket counts, and percentiles is serialized and parsed back
- **THEN** every scalar, name, policy, bound, bucket count, and percentile survives the round-trip

#### Scenario: State record JSON round-trip

- **WHEN** a `MetricRecord` for a state gauge carrying a string value is serialized and parsed back
- **THEN** the string value and the `MetricKind::State` classification survive the round-trip

#### Scenario: Unit round-trips

- **WHEN** a `MetricRecord` whose identity carries a unit is serialized and parsed back
- **THEN** the unit string survives the round-trip, and an empty unit stays empty

#### Scenario: Batch JSON round-trip

- **WHEN** a `MetricBatchSnapshot` containing multiple records is serialized and parsed back
- **THEN** the record count, collection timestamp, and per-record contents survive the round-trip

#### Scenario: Snapshot diagnostics render

- **WHEN** `to_string()` is called on a record and a batch, and the free functions are called on the same objects
- **THEN** each member/free pair returns equal, non-empty strings, and `std::format` renders the same text
