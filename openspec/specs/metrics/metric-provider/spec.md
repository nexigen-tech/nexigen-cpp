# metrics/metric-provider Specification

## Purpose

Defines the only supported construction path for metrics — `MetricProvider` creates `Meter` handles, which create instruments in a pre-allocated registry — and the background harvesting, collection-period, CPU-affinity, and control-plane behavior of the provider.

## Requirements

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

`MetricProviderConfig` SHALL carry a `ThreadSpec` for the existing harvest worker instead of a uint64 core mask. When harvesting is enabled, the worker SHALL apply the spec to itself before delivering any batch, supporting CPUs above 63, naming, and Other/FIFO/RR scheduling. An empty CPU set SHALL leave affinity unchanged. Best-effort failures SHALL be reported without stopping collection; a `Required` failure SHALL prevent the harvest loop and publish a failed `startup_status()` without leaking a worker. A zero interval SHALL create no worker and apply no attributes.

#### Scenario: Harvester affinity is applied

- **WHEN** the provider starts with valid worker attributes and the test queries them on the worker
- **THEN** the name, affinity, policy, and priority are applied and every background sink callback runs on that worker

#### Scenario: Invalid mask is visible

- **WHEN** a requested affinity or scheduler operation fails under `BestEffort`
- **THEN** `startup_status()`/the thread report exposes the failure and batches continue with any successfully applied attributes

#### Scenario: Required failure prevents harvesting

- **WHEN** a requested attribute fails under `Required`
- **THEN** no harvest loop runs, `startup_status()` is an error, and provider destruction is safe without a live worker

#### Scenario: CPU above 63 is supported

- **WHEN** the worker spec contains a permitted CPU above 63
- **THEN** the harvester uses it without a uint64 mask conversion

#### Scenario: Zero mask leaves affinity alone

- **WHEN** the worker CPU set is empty
- **THEN** the worker inherits the process affinity and no pinning call is required to succeed

#### Scenario: Disabled harvesting applies nothing

- **WHEN** `collect_interval` is zero
- **THEN** no worker is created and no thread-attribute system call is made

### Requirement: Provider control plane

The provider SHALL expose `collect_now() noexcept -> core::Status`, `flush() noexcept -> core::Status`, `startup_status() const noexcept -> core::Status`, and `harvest_status() const noexcept -> core::Status`. Failures from harvesting or the bound sink SHALL propagate through these returns, and the provider SHALL remain usable after a failed collection. After quiescence, `collect_now()` and `flush()` SHALL return `NotInitialized` while `startup_status()`, `harvest_status()`, and read-only diagnostics remain available.

#### Scenario: Flush failure is propagated

- **WHEN** the bound sink reports an I/O failure from `flush()`
- **THEN** `MetricProvider::flush()` returns that failure as a `core::Status` error arm

#### Scenario: Collection errors do not crash

- **WHEN** the sink rejects a batch or an internal harvest operation fails
- **THEN** `collect_now()` reports the failure through its `core::Status` and the provider remains usable

#### Scenario: Startup status reports success by default

- **WHEN** the provider starts with an empty worker CPU set and a healthy sink
- **THEN** `startup_status()` returns success

#### Scenario: Harvest status reflects the last collection

- **WHEN** a background collection fails and `harvest_status()` is read
- **THEN** it returns that failure without performing another collection

#### Scenario: Control plane closes after quiescence

- **WHEN** `collect_now()` or `flush()` is called on a quiesced provider
- **THEN** each returns `ErrorCode::NotInitialized` and no sink I/O occurs

### Requirement: Provider shutdown

Destruction SHALL release the pre-allocated registry storage without allocating or throwing. A provider that has not been quiesced SHALL stop and join the harvester, deliver one final collection of pending values, and flush the bound sink before releasing storage; a quiesced provider SHALL NOT collect or flush again. Instrument and meter handles SHALL NOT outlive their provider.

#### Scenario: Clean shutdown drains the sink

- **WHEN** a provider with pending writes is destroyed
- **THEN** the harvester stops, a final batch carrying the pending values is delivered, `flush()` reaches the sink, and no sanitizer reports a race or leak

#### Scenario: Handles do not outlive the provider

- **WHEN** a `Meter` or instrument is used after its provider is destroyed
- **THEN** the behavior is a documented lifetime violation, and debug/test builds detect it through a provider liveness flag

#### Scenario: Quiesced provider is not drained again

- **WHEN** a provider that has already been quiesced is destroyed
- **THEN** the destructor skips the final collection and sink flush, and no duplicate batch reaches the sink

### Requirement: Provider configuration serialization

`MetricProvider_config` SHALL implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` for its complete visible state under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization, including the complete worker thread spec.

#### Scenario: Configuration JSON round-trip

- **WHEN** a `MetricProvider_config` with a 10 ms interval and a named worker on CPUs `{2, 70}` with FIFO priority 30 is serialized and parsed back
- **THEN** the JSON carries the interval in nanoseconds and every worker attribute, and the parsed configuration compares equal to the original

#### Scenario: Configuration renders diagnostics

- **WHEN** `to_string()` is called on a configuration and the free `to_string` is called on the same object
- **THEN** both return equal, non-empty strings describing the interval and worker spec

#### Scenario: Configuration formats

- **WHEN** `std::format("{}", config)` is evaluated
- **THEN** the result equals the configuration's `to_string()` text

### Requirement: Record and batch snapshot serialization

`MetricRecord` and `MetricBatchSnapshot` SHALL each implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter` support, so harvested batches round-trip through JSON diagnostics. `MetricBatchSnapshot` SHALL carry a `utc_resolved` marker and SHALL derive `utc_ns` from `TimeBaseClock::utc_from_canonical(stamp)`; when the base cannot resolve the window, `utc_resolved` SHALL be false.

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
- **THEN** the record count, collection timestamp, UTC resolution marker, and per-record contents survive the round-trip

#### Scenario: Resolved window carries UTC

- **WHEN** a batch is harvested while the time base resolves the window stamp
- **THEN** `utc_resolved` is true and `utc_ns` equals the published-base conversion of the stamp

#### Scenario: Unresolved window is explicit

- **WHEN** the time base cannot resolve the window stamp
- **THEN** `utc_resolved` is false, no raw `CLOCK_REALTIME` sample is taken, and JSON omits the UTC value

#### Scenario: Snapshot diagnostics render

- **WHEN** `to_string()` is called on a record and a batch, and the free functions are called on the same objects
- **THEN** each member/free pair returns equal, non-empty strings, and `std::format` renders the same text

### Requirement: Meter publication is serialized with harvesting

`get_meter` SHALL perform its lookup, creation, and registry publication under
the provider's harvest mutex — the same mutex the harvester holds while
resetting pools — so concurrent `get_meter` calls, a running harvester, and
conflicting-version lookup racing creation are linearized at the mutex. The
call stays cold-path; instrument hot paths SHALL NOT touch the mutex.

#### Scenario: get_meter races the harvester

- **WHEN** one thread calls `get_meter` on a new identity while the harvester
  thread is resetting pools under the harvest mutex (TSan build)
- **THEN** TSan reports no data race, and the harvester's sweep observes either
  no reference to the not-yet-published meter or a fully constructed meter

#### Scenario: Concurrent identical get_meter hands out one meter

- **WHEN** two threads call `get_meter` concurrently with the same
  name+version identity
- **THEN** registry capacity is not double-consumed: both calls return a
  handle to the single constructed meter, and a concurrent
  conflicting-version call still reports `InvalidArgument`

### Requirement: Meter and counter model the core concepts

`metrics::Meter` SHALL satisfy `core::InstrumentMeterLike`, and each created instrument SHALL
satisfy its core concept: `CounterLike`, `GaugeLike`, `StateGaugeLike`,
`DescriptiveStatisticsLike`, or `HistogramLike`. The metrics module SHALL remain the only owner of instruments
and SHALL NOT depend on `com`, `logging`, `tracing`, or `chrono`.

#### Scenario: Concept conformance is pinned

- **WHEN** the metrics tests are compiled
- **THEN** `static_assert(core::InstrumentMeterLike<Meter>)` and every instrument conformance
  assertion hold

#### Scenario: Factory errors match the concept contract

- **WHEN** a counter name is empty, over-long, or duplicated
- **THEN** `create_counter` returns `InvalidArgument` and no instrument is registered

#### Scenario: Metrics has no telemetry dependency

- **WHEN** the metrics target's link line is inspected
- **THEN** it links only `core` and `chrono` and names no telemetry module

### Requirement: Explicit provider quiescence

`MetricProvider::quiesce() noexcept` SHALL return `core::Status`, stop and join the harvester, perform one final collection, flush the bound sink, and seal the provider while keeping registry storage alive. It SHALL be idempotent, allocate nothing, and throw nothing.

#### Scenario: Final collection is delivered

- **WHEN** instruments hold pending values and `quiesce()` is called
- **THEN** one final batch carrying those values is delivered to the sink and flushed before the call returns

#### Scenario: Storage stays alive and creation closes

- **WHEN** `quiesce()` returns and an already-issued meter or instrument is used
- **THEN** the handle is memory-safe, `get_meter()` returns `NotInitialized`, and no new instrument is registered

#### Scenario: Repeated quiescence is harmless

- **WHEN** `quiesce()` is called twice
- **THEN** the second call returns success, the sink is not contacted again, and no sanitizer reports a problem

#### Scenario: Destruction does not drain twice

- **WHEN** a quiesced provider is destroyed
- **THEN** no collection or sink flush occurs during destruction, and storage is released without allocating

#### Scenario: Sink failure still seals the provider

- **WHEN** the bound sink reports a flush failure during `quiesce()`
- **THEN** the call returns that failure, the provider is still sealed, and destruction performs no second flush

#### Scenario: Diagnostics remain available

- **WHEN** `to_string()`, `startup_status()`, or the worker report is read after `quiesce()`
- **THEN** each returns its normal content and no memory error occurs

### Requirement: Provider diagnostics binding

`MetricProvider` SHALL expose `bind_diagnostics(DiagnosticBinding)` with a templated logger convenience and `harvest_status() const noexcept -> core::Status` reporting the last collection and sink-flush result. The status SHALL be readable without collecting, flushing, or contacting the sink.

#### Scenario: A bound callback receives provider events

- **WHEN** a provider with a bound callback reports a transition
- **THEN** the callback receives exactly one event and the provider retains no ownership of the logger

#### Scenario: Unbound provider stays silent

- **WHEN** no binding is installed and collections fail
- **THEN** no callback is invoked and `harvest_status()` still exposes the failure

#### Scenario: Status is pollable without I/O

- **WHEN** `harvest_status()` is read between collections
- **THEN** it returns the last result without collecting, flushing, or contacting the sink

### Requirement: Provider diagnostic transitions

A bound provider SHALL emit `Started` once after the harvester starts, `Degraded` on the first failing transition, `Recovered` when a later collection succeeds, and `Quiesced` after sealing — at most one event per transition. Emission SHALL be cold-path only, SHALL allocate nothing, and SHALL stop after quiescence.

#### Scenario: Start is reported once

- **WHEN** a bound provider starts its harvester
- **THEN** exactly one `Started` event is delivered

#### Scenario: A failing harvest degrades once

- **WHEN** background collection or sink flush fails repeatedly
- **THEN** exactly one `Degraded` event carrying the first error is delivered, and later identical failures deliver nothing

#### Scenario: Recovery is reported

- **WHEN** a collection succeeds after a reported failure
- **THEN** exactly one `Recovered` event is delivered and the next failure reports again

#### Scenario: Quiescence is reported last

- **WHEN** `quiesce()` completes and the provider is sealed
- **THEN** exactly one `Quiesced` event is delivered and no later event is emitted
