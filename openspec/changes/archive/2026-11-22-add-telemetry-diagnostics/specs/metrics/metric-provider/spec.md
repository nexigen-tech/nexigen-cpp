# Spec Delta

## ADDED Requirements

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

## MODIFIED Requirements

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
