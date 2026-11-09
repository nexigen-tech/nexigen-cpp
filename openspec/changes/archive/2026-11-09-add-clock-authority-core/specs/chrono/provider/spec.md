# Spec Delta

## Purpose

Defines the threadless clock authority: a `Provider` that owns the calibrated per-source stampers issued to producers alone (`__rdtsc()`-based monotonic stamps with zero allocation and zero wall-clock reads), samples the signed UTC offset for cold-path conversion, and delivers UTC-offset snapshots to a bound sink only when triggered. The capability carries the provider concept contract, the issuance/expiry error model, and the provider configuration serialization.

## ADDED Requirements

### Requirement: Provider construction and sink binding

A `Provider` SHALL be constructed from a `ProviderConfig` with a bound `concepts::Sink` sink type-erased through plain function pointers (no virtual dispatch, no RTTI), fixed pre-allocated storage, and no background thread, and construction SHALL NOT start any thread. Construction SHALL NOT deliver any snapshot and the `snapshot_now()` and UTC authority methods SHALL be usable immediately after construction. The bound sink MUST outlive the provider.

#### Scenario: Construction succeeds with a valid config and sink

- **WHEN** a provider is constructed with a valid `ProviderConfig` and a healthy sink
- **THEN** construction succeeds and no thread is created

#### Scenario: Construction delivers nothing

- **WHEN** a provider is constructed and observed before any trigger
- **THEN** the sink has received no snapshot

#### Scenario: Authority is usable immediately after construction

- **WHEN** `snapshot_now()` and `utc_offset()` are called right after construction
- **THEN** the offsets are sampled and exactly one snapshot is delivered to the bound sink

### Requirement: Stamper concept and issuance

`Provider` SHALL satisfy `concepts::Provider` by exposing a nested `Stamper` type satisfying `concepts::Stamper` (`sample() noexcept -> Timestamp`) and issuing pre-calibrated per-source stampers through `acquire() noexcept -> core::Result<Stamper*>` on the cold path. Each issued source keeps its appending state single-writer, so a hot `sample()` stays allocation-free, atomics-free, and free of wall-clock reads.

#### Scenario: Every issued stamper satisfies the stamper concept

- **WHEN** a provider instance of type `Provider` is static_asserted against `concepts::Provider`
- **THEN** the concept checks pass at compile time and the nested `Stamper` satisfies `concepts::Stamper`

#### Scenario: Every acquire yields a distinct source

- **WHEN** `acquire()` is called twice on a prepared provider
- **THEN** each resulting source has its own non-decreasing sample series independent of the other's history

#### Scenario: Issuance allocates nothing

- **WHEN** `acquire()` is executed under an armed allocation arena on a prepared provider
- **THEN** no `operator new`, `delete`, `malloc`, `free`, or container growth is observed

### Requirement: Fixed stamper bank capacity

The stamper bank SHALL hold a compile-time-sized pool of sources. An `acquire()` on a fully issued bank SHALL report `ResourceExhausted` and leave the bank state unchanged.

#### Scenario: Bank exhaustion reports ResourceExhausted

- **WHEN** the provider's bank is fully issued and `acquire()` is called once more
- **THEN** the call returns `ResourceExhausted` and the bank state is unchanged

### Requirement: Calibration gates issuance

`Provider` SHALL provide `prepare() noexcept -> core::Status` that calibrates every bank source on the cold calling thread, and `acquire()` before a completed `prepare()` SHALL report `NotInitialized`. A calibration failure SHALL surface through `prepare()` as the sampleable clock error and leave the provider safe to prepare again.

#### Scenario: Prepared provider issues calibrated sources

- **WHEN** `prepare()` succeeds and `acquire()` is called
- **THEN** the issued source produces a monotonic-raw `Timestamp` immediately

#### Scenario: Issuance before prepare is rejected

- **WHEN** `acquire()` is called on a provider whose `prepare()` was never completed
- **THEN** the call reports `NotInitialized` and no source is issued

#### Scenario: Calibration failure is surfaced and retrievable

- **WHEN** one of the calibration clock reads fails
- **THEN** `prepare()` reports the failure and a retried `prepare()` may calibrate the bank and succeed

### Requirement: UTC offset authority

`Provider` SHALL provide `utc_offset() noexcept -> std::int64_t` sampling the signed realtime minus monotonic-raw offset at the call instant (fresh per call), and `utc_now_ns() noexcept -> std::int64_t` returning the current wall time as nanoseconds since the epoch. The pure helper `utc_from_mono_ns(monotonic_ns, offset_ns)` remains a free function.

#### Scenario: Offset magnitude follows the clock relationship

- **WHEN** `utc_offset()` is sampled
- **THEN** the value is a signed `std::int64_t` whose magnitude is bounded by the realtime-monotonic relationship

#### Scenario: utc now renders a plausible UTC instant

- **WHEN** `utc_now_ns()` is called twice
- **THEN** both calls yield values in the valid epoch range and differ by no more than the scheduling tolerance

#### Scenario: Conversion composes with the stored offset

- **WHEN** a monotonic stamp is converted through `utc_from_mono_ns` with the sampled `utc_offset()`
- **THEN** the composed value equals the monotonic nanoseconds plus the sampled offset, converted once per call

### Requirement: Snapshot content

Every snapshot the provider delivers SHALL carry a monotonic-raw `stamp` (the series key) and the signed UTC offset (`realtime` minus `monotonic_raw`) sampled at that instant. `boottime` SHALL be reserved in the clock domains but not sampled or recorded in this version.

#### Scenario: Snapshot carries the key and the offset

- **WHEN** a delivered snapshot is inspected
- **THEN** it holds a `chrono::Timestamp` stamp in the monotonic-raw domain and a signed `std::int64_t` realtime offset

#### Scenario: boottime is not recorded

- **WHEN** a delivered snapshot is serialized
- **THEN** its content names only the monotonic-raw stamp and the UTC offset, with no boottime field

### Requirement: Manual snapshot delivery

The provider SHALL provide `snapshot_now() noexcept -> core::Status` and `flush() noexcept -> core::Status`; `snapshot_now()` SHALL sample stamp and offset at one instant and deliver exactly one snapshot to the bound sink on the calling thread before returning. Failures from sampling or the bound sink SHALL propagate through these returns, and the provider SHALL remain usable after a failed snapshot.

#### Scenario: Manual snapshot is deterministic

- **WHEN** `snapshot_now()` is called once
- **THEN** exactly one snapshot is delivered to the sink on the calling thread before the call returns

#### Scenario: Snapshot errors do not crash

- **WHEN** the sink rejects a snapshot or a clock read fails
- **THEN** `snapshot_now()` reports the failure through its `core::Status` and the provider remains usable thereafter

#### Scenario: Flush failure is propagated

- **WHEN** the bound sink reports an I/O failure from `flush()`
- **THEN** `flush()` returns that failure as a `core::Status` error arm

### Requirement: Concurrent manual triggers never interleave

Sequential and concurrent manual triggers SHALL deliver complete snapshots without interleaving each other's sink writes.

#### Scenario: Concurrent manual triggers never interleave

- **WHEN** two threads call `snapshot_now()` repeatedly against a recording sink
- **THEN** the sink observes a complete snapshot per accepted call without partial interleavings

### Requirement: Steady-state allocation of the authority

Sampling issued sources and delivering manual snapshots SHALL perform no heap allocation: all provider working state (bank, bindings, serialization lock) is fixed members, and diagnostics that allocate stay out of the steady-state paths.

#### Scenario: Steady-state sampling allocates nothing

- **WHEN** issued stampers sample and the provider delivers snapshots repeatedly after preparation under an armed allocation arena
- **THEN** no `operator new`, `delete`, `malloc`, `free`, or container growth is observed

### Requirement: Provider shutdown

Destruction of a provider SHALL flush the bound sink and release its fixed storage, without delivering an automatic final snapshot (final delivery is a `ChronoWorker` shutdown duty) and without throwing. Destruction of a provider whose worker never existed SHALL be safe; the worker never lived in this type.

#### Scenario: Destruction flushes the bound sink

- **WHEN** a prepared, used provider is destroyed
- **THEN** `flush()` reaches the sink and the provider releases its storage without error

#### Scenario: Destruction delivers no hidden snapshot

- **WHEN** a manual provider whose user did not call `snapshot_now()` last is destroyed
- **THEN** the sink receives no additional snapshot beyond those explicitly delivered

### Requirement: Provider configuration serialization

`ProviderConfig` SHALL carry the snapshot-triggering interval, the complete worker thread spec, and the provider kind, and SHALL implement member plus free `to_string() const`, non-throwing `to_json`/`from_json` for its complete visible state under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization over its canonical rendering.

#### Scenario: Configuration JSON round-trip

- **WHEN** a `ProviderConfig` with a non-zero interval, a named FIFO worker thread spec above CPU 63, and kind `daemon` is serialized and parsed back under `JSON_NOEXCEPTION=1`
- **THEN** the JSON carries the interval in nanoseconds, every worker attribute, and the kind, and the parsed configuration compares equal to the original

#### Scenario: Configuration renders and formats

- **WHEN** `to_string()`, the free `to_string`, and `std::format("{}", config)` are applied
- **THEN** the member and free results are equal non-empty strings and the formatter renders the same text

#### Scenario: Invalid JSON degrades without terminating

- **WHEN** `from_json` receives a JSON integer or null for a field expecting canonical strings, under `JSON_NOEXCEPTION=1`
- **THEN** the call returns normally and the target configuration keeps its previous values

### Requirement: Provider diagnostics

`Provider` SHALL provide a cold-path `to_string() const` member plus free helper and a `std::formatter` specialization, reporting its configuration and the counting-state (prepared flag, issued sources, capacity) for post-mortem diagnosis.

#### Scenario: Provider renders configuration and state

- **WHEN** `to_string()` and `std::format("{}", provider)` are evaluated on a prepared provider
- **THEN** the member text names the interval, the kind, the bank capacity, and the issued-source count, and the formatter renders the same text
