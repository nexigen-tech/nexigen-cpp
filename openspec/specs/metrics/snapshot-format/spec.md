# metrics/snapshot-format Specification

## Purpose

Provides the cold-path JSON serialization bodies for metric snapshot value types (`MetricIdentity`, `ScalarValue`, `StatisticsSnapshot`, `HistogramSnapshot`, `StateValue`, `MetricRecord`, `MetricBatchSnapshot`) and for `MetricProviderConfig`. Every allocating or nlohmann operation for the snapshot model lives in this unit so that the recording and harvesting paths remain allocation-free.

## Requirements

### Requirement: Identity and scalar snapshots round-trip safely

`MetricIdentity::from_json` SHALL copy up to `max_name_length` bytes from each string field and SHALL ignore missing or non-string fields. `ScalarValue::from_json` SHALL accept signed/unsigned integers and doubles with type checks and SHALL update `is_double` only from a boolean field.

#### Scenario: MetricIdentity names are bounded

- **WHEN** a JSON `metric_name` string is 64 bytes
- **THEN** it is copied in full; if it is 65 bytes, it is truncated to `max_name_length`

#### Scenario: Scalar counter accepts signed and unsigned inputs

- **WHEN** `counter` is present as a positive signed integer or an unsigned integer
- **THEN** `ScalarValue.counter` is set to the magnitude

### Requirement: Statistics and histogram snapshots use typed numeric readers

`StatisticsSnapshot::from_json` and `HistogramSnapshot::from_json` SHALL read each numeric field through helpers that check the JSON type (`is_number`, `is_number_unsigned`, `is_number_integer`) and ignore missing or incompatible fields. Histogram bucket arrays SHALL be bounded by `bucket_count` and `max_snapshot_buckets`.

#### Scenario: Histogram bucket arrays are capped

- **WHEN** the JSON `buckets_lower` array is longer than `bucket_count`
- **THEN** only the first `bucket_count` entries are written

#### Scenario: p99.9 field maps to `p999`

- **WHEN** the JSON field `p99.9` is present as a number
- **THEN** `HistogramSnapshot.p999` is updated

### Requirement: MetricRecord deserialization selects the union member by kind

`MetricRecord::from_json` SHALL read `identity` and the four policy enums first, then deserialize exactly one payload object according to `kind`: `scalar` for `Counter`/`Gauge`, `statistics` for `DescriptiveStatistics`, `state` for `State`, and `histogram` for all histogram kinds. Only the active union member SHALL be touched.

#### Scenario: Counter record ignores non-scalar payload fields

- **WHEN** a `Counter` record JSON also contains a `histogram` object
- **THEN** only `scalar` is parsed and `histogram` is ignored

#### Scenario: Unknown kind leaves payload untouched

- **WHEN** the JSON `kind` string is not a recognized `MetricKind`
- **THEN** no payload field is parsed and the destination keeps its previous payload

### Requirement: MetricBatchSnapshot deserialization is array-length safe

`MetricBatchSnapshot::from_json` SHALL set `stamp_ns`, `utc_ns`, and `record_count` only from valid numbers. The `records` array SHALL be walked only when `out.records` is non-null, and SHALL stop at `record_capacity`. If fewer records were parsed than `record_count` claimed, the actual parsed count SHALL be stored.

#### Scenario: Records array shorter than claimed count

- **WHEN** `record_count` is 5 but the JSON array contains 3 records
- **THEN** 3 records are written and `out.record_count` becomes 3

#### Scenario: Null records pointer skips array parsing

- **WHEN** `out.records` is null
- **THEN** the `records` JSON array is ignored and no write occurs

### Requirement: MetricProviderConfig deserialization uses safe nlohmann accessors

`MetricProviderConfig::from_json` SHALL read numeric fields with type checks, SHALL prefer the canonical `worker_thread` object over the legacy `core_mask`, SHALL clamp `collect_interval_ns` to a representable `std::chrono::nanoseconds` range, and SHALL leave unspecified capacities unchanged.

#### Scenario: Legacy core_mask sets worker CPUs

- **WHEN** only `core_mask` is present as an unsigned number
- **THEN** `worker_thread.cpus` is set from the mask

### Requirement: All deserialization is exception-free under `JSON_NOEXCEPTION=1`

Every `from_json` overload in this unit SHALL use `is_*` guards and pointer accessors rather than exceptions. A wrong shape SHALL be a silent no-op for the affected fields.

#### Scenario: Parsing runs under `-fno-exceptions`

- **WHEN** the unit is compiled with `JSON_NOEXCEPTION=1`
- **THEN** malformed JSON inputs do not throw and the binary links successfully

### Requirement: Serialization helpers do not change hot-path contracts

No function added or modified by this refactor SHALL be placed in a hot header or called from a recording/harvesting path. The hot headers (`metric_snapshot.hpp`, `metric_provider.hpp`) SHALL keep their current inline, allocation-free contracts.

#### Scenario: Metric recording and harvest hot paths remain unchanged

- **WHEN** `record()`, `read()`, and provider harvest run in the existing benchmarks
- **THEN** they do not include `snapshot_format.cpp` machinery and their gate times are unchanged

### Requirement: Complexity of decomposed functions stays within the project target

Every function introduced or modified by this refactor SHALL have cyclomatic complexity ≤ 15 as measured by lizard. The overall average complexity for `src/metrics/snapshot_format.cpp` SHALL not increase.

#### Scenario: Post-refactor lizard report

- **WHEN** lizard is run over `src/metrics/snapshot_format.cpp`
- **THEN** no function in the file exceeds complexity 15
