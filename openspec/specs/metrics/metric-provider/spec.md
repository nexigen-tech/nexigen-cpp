# metrics/metric-provider Specification

## Purpose

Defines the only supported construction path for metrics — `MetricProvider` creates `Meter` handles, which create instruments in a pre-allocated registry — and the background harvesting, collection-period, CPU-affinity, and control-plane behavior of the provider.

## Requirements

### Requirement: Factory hierarchy and passkey construction

Metrics SHALL be created exclusively through a `Meter` obtained from `MetricProvider::get_meter(name, version)`. Instrument constructors SHALL be guarded by a private `MeterPasskey` and `friend class Meter`; user code SHALL NOT be able to construct `Counter`, `Gauge`, `DescriptiveStatistics`, `ExplicitBucketHistogram`, or `ExponentialHistogram` directly.

#### Scenario: Direct construction is rejected

- **WHEN** `std::is_constructible_v` is evaluated for each instrument type and for `MeterPasskey` with every publicly reachable argument combination
- **THEN** every result is `false`, and a scratch translation unit attempting direct construction fails to compile

#### Scenario: Meter is the only instrument factory

- **WHEN** user code needs a counter, gauge, statistics, or histogram
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

### Requirement: Pre-allocated aligned metric registry

`MetricProvider` SHALL own fixed-capacity, `alignas(64)`-aligned storage for instrument objects, meter identities, and metric names, reserved during initialization. Creating a meter or instrument and harvesting SHALL perform zero dynamic heap allocations, and registry traversal SHALL be contiguous in memory.

#### Scenario: No allocation during creation or harvest

- **WHEN** a test allocation arena is armed and the full registry capacity of instruments is created and harvested repeatedly
- **THEN** no `operator new`, `delete`, `malloc`, `free`, or container growth is observed

#### Scenario: Storage is cache-line aligned

- **WHEN** the registry base address and each instrument slot are inspected
- **THEN** their addresses are multiples of 64 bytes

#### Scenario: Capacity exhaustion is reported

- **WHEN** instrument or meter creation is attempted after the configured capacity for that kind is exhausted
- **THEN** the creation call returns `core::ErrorCode::BufferFull` and does not corrupt existing instruments

#### Scenario: Invalid names are rejected

- **WHEN** a meter or instrument is requested with an empty name, a name longer than 63 characters, or a duplicate instrument name within the same meter
- **THEN** the call returns `core::ErrorCode::InvalidArgument` and consumes no registry slot

### Requirement: Meter instrument creation

`Meter` SHALL expose creation methods for counters, gauges, descriptive statistics, explicit-bucket histograms, and exponential histograms, each accepting the instrument name and the policy template parameters and creation arguments that determine its behavior. Every creation call SHALL return `core::Result` and never throw.

#### Scenario: Counter and gauge creation

- **WHEN** a meter creates a counter and a gauge with valid names and policies
- **THEN** both calls succeed, the instruments record values, and a later harvest reports each with the matching `MetricKind`

#### Scenario: Histogram creation parameters

- **WHEN** a meter creates an explicit histogram with strictly increasing finite bounds or an exponential histogram with a valid scale
- **THEN** creation succeeds and the stored configuration is reflected in the snapshot

#### Scenario: Invalid histogram parameters

- **WHEN** explicit bounds are empty, unsorted, duplicated, non-finite, or exceed the inline capacity, or the exponential scale is outside its supported range
- **THEN** the call returns `core::ErrorCode::InvalidArgument` and no instrument is registered

#### Scenario: Instrument types are distinct

- **WHEN** the same name is used for two different instrument kinds under one meter
- **THEN** the second creation returns `core::ErrorCode::InvalidArgument`

### Requirement: Background harvesting and collection period

`MetricProvider` SHALL bind to a `MetricSink` and run a background harvester on a `std::jthread`, delivering a batch snapshot every configured `collect_interval` (a `std::chrono::nanoseconds` field of `MetricProvider_config`). A zero interval SHALL disable the background thread and leave `collect_now()` as the sole collection trigger.

#### Scenario: Configured period is honored

- **WHEN** the provider is configured with a 10 ms interval and a counting sink, and the test waits for several periods
- **THEN** the sink observes multiple non-decreasing batches whose arrival spacing is consistent with 10 ms within a documented scheduling tolerance

#### Scenario: Zero interval disables background collection

- **WHEN** `collect_interval` is zero and the provider is otherwise initialized
- **THEN** no batch is delivered until `collect_now()` is called explicitly

#### Scenario: Manual collection is deterministic

- **WHEN** records are written and `collect_now()` is called once
- **THEN** exactly one batch containing every registered metric is delivered to the sink on the calling thread before the call returns

#### Scenario: Harvest snapshots are consistent

- **WHEN** a batch is delivered
- **THEN** every record carries the meter name, version, metric name, kind, policies, and the values observed for that collection window

### Requirement: CPU core affinity

When `MetricProvider_config::core_mask` is non-zero, the background harvester thread SHALL pin itself to exactly those CPU cores with `pthread_setaffinity_np` before delivering any batch, so collection and sink I/O cannot preempt unpinned real-time threads. A zero mask SHALL leave affinity unchanged.

#### Scenario: Harvester affinity is applied

- **WHEN** the provider starts with a non-zero mask containing one or more permitted CPUs and the test queries the worker with `pthread_getaffinity_np`
- **THEN** the worker's affinity mask equals the configured mask and every background sink callback runs on that worker

#### Scenario: Invalid mask is visible

- **WHEN** the configured mask contains no permitted CPU or `pthread_setaffinity_np` fails
- **THEN** `startup_status()` reports `core::ErrorCode::SystemError` and the failure is observable rather than silently ignored

#### Scenario: Zero mask leaves affinity alone

- **WHEN** the provider starts with `core_mask == 0`
- **THEN** the worker inherits the process affinity and no pinning call is required to succeed

### Requirement: Provider control plane

The provider SHALL expose `collect_now() noexcept -> core::Status`, `flush() noexcept -> core::Status`, and `startup_status() const noexcept -> core::Status`. Failures from harvesting or the bound sink SHALL propagate through these returns, and the provider SHALL remain usable after a failed collection.

#### Scenario: Flush failure is propagated

- **WHEN** the bound sink reports an I/O failure from `flush()`
- **THEN** `MetricProvider::flush()` returns that failure as a `core::Status` error arm

#### Scenario: Collection errors do not crash

- **WHEN** the sink rejects a batch or an internal harvest operation fails
- **THEN** `collect_now()` reports the failure through its `core::Status` and the provider remains usable

#### Scenario: Startup status reports success by default

- **WHEN** the provider starts with a zero core mask and a healthy sink
- **THEN** `startup_status()` returns success

### Requirement: Provider shutdown

Destruction SHALL stop and join the harvester, deliver one final collection of pending values, and flush the bound sink before releasing storage, without allocating or throwing on the shutdown path. Instrument and meter handles SHALL NOT outlive their provider.

#### Scenario: Clean shutdown drains the sink

- **WHEN** a provider with pending writes is destroyed
- **THEN** the harvester stops, a final batch carrying the pending values is delivered, `flush()` reaches the sink, and no sanitizer reports a race or leak

#### Scenario: Handles do not outlive the provider

- **WHEN** a `Meter` or instrument is used after its provider is destroyed
- **THEN** the behavior is a documented lifetime violation, and debug/test builds detect it through a provider liveness flag

### Requirement: Provider configuration serialization

`MetricProvider_config` SHALL implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` for its complete visible state under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization.

#### Scenario: Configuration JSON round-trip

- **WHEN** a `MetricProvider_config` with a 10 ms interval and mask `0x3` is serialized and parsed back
- **THEN** the JSON carries the interval in nanoseconds and the mask, and the parsed configuration compares equal to the original

#### Scenario: Configuration renders diagnostics

- **WHEN** `to_string()` is called on a configuration and the free `to_string` is called on the same object
- **THEN** both return equal, non-empty strings describing the interval and mask

#### Scenario: Configuration formats

- **WHEN** `std::format("{}", config)` is evaluated
- **THEN** the result equals the configuration's `to_string()` text

### Requirement: Record and batch snapshot serialization

`MetricRecord` and `MetricBatchSnapshot` SHALL each implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter` support, so harvested batches round-trip through JSON diagnostics.

#### Scenario: Record JSON round-trip

- **WHEN** a `MetricRecord` for a histogram carrying bounds, bucket counts, and percentiles is serialized and parsed back
- **THEN** every scalar, name, policy, bound, bucket count, and percentile survives the round-trip

#### Scenario: Batch JSON round-trip

- **WHEN** a `MetricBatchSnapshot` containing multiple records is serialized and parsed back
- **THEN** the record count, collection timestamp, and per-record contents survive the round-trip

#### Scenario: Snapshot diagnostics render

- **WHEN** `to_string()` is called on a record and a batch, and the free functions are called on the same objects
- **THEN** each member/free pair returns equal, non-empty strings, and `std::format` renders the same text
