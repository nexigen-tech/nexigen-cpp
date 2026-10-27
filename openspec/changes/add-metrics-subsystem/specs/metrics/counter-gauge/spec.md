# Spec Delta

## Purpose

Defines the `Counter` and `Gauge` hot-path instruments: their recording surface, single-threaded and multi-threaded storage contracts, and their cumulative versus reset-on-read windows.

## ADDED Requirements

### Requirement: Counter recording surface

`Counter<ConcurrencyPolicy, AccessPolicy, ResetPolicy>` SHALL support `inc() noexcept` and `add(std::uint64_t) noexcept`. In `SingleThreaded` mode updates SHALL touch plain non-atomic storage, targeting sub-nanosecond-class hot-path cost with no atomic instructions.

#### Scenario: Single-threaded increments accumulate exactly

- **WHEN** one thread calls `inc()` three times and `add(7)` once
- **THEN** the counter reads `10` with exact arithmetic and no lost updates

#### Scenario: Readable counter read and snapshot

- **WHEN** a `Readable` counter accumulates five counts and application code calls `read()` and `snapshot()`
- **THEN** `read()` returns `5` and `snapshot()` returns a `MetricRecord` carrying `MetricKind::Counter` and the value `5`

#### Scenario: Single-threaded hot-path latency gate

- **WHEN** the Release latency benchmark executes a sustained `inc()` loop
- **THEN** the median per-call latency stays at or below the documented Release gate for single-threaded counters

### Requirement: Counter multi-threaded sharding

In `MultiThreaded` mode `Counter` SHALL update cache-line-aligned (`alignas(64)`) per-thread shards with wait-free operations and no cross-core CAS contention. Up to 32 threads SHALL hold exclusive `thread_local`-registered shards; further threads SHALL share a fallback shard whose atomic updates keep totals exact under contention.

#### Scenario: Concurrent increments are exact

- **WHEN** 32 threads each perform one million `inc()` calls on one `MultiThreaded` counter
- **THEN** the aggregated count is exactly 32 million and no update is lost

#### Scenario: Threads beyond the shard pool stay exact

- **WHEN** more than 32 threads record into the same `MultiThreaded` counter
- **THEN** the cumulative total remains exact through the shared fallback shard

#### Scenario: Recording never blocks on other threads

- **WHEN** one recording thread is descheduled mid-update
- **THEN** other threads' `inc()` calls still complete in bounded time without waiting for it

#### Scenario: Multi-threaded hot-path latency gate

- **WHEN** the Release latency benchmark executes `inc()` from a registered thread
- **THEN** the median per-call latency stays at or below the documented Release gate for multi-threaded counters

### Requirement: Counter reset windows

With `ResetPolicy::Cumulative`, reads and harvests SHALL leave totals intact. With `ResetPolicy::ResetOnRead`, `read()`, `snapshot()`, and harvesting SHALL report only the delta since the previous read or harvest and reset the window. In `MultiThreaded` mode a record racing a reset SHALL be counted in exactly one window.

#### Scenario: Cumulative totals persist

- **WHEN** a cumulative counter is read and harvested repeatedly after accumulating counts
- **THEN** every observation reports the full total and recording continues from it

#### Scenario: Reset-on-read reports window deltas

- **WHEN** a reset-on-read counter receives `add(5)`, is read twice, receives `add(3)`, and is read again
- **THEN** the reads return `5`, `0`, and `3`

#### Scenario: Records racing a reset land once

- **WHEN** recording threads run concurrently with periodic harvests on a `MultiThreaded` reset-on-read counter
- **THEN** the sum over all windows equals the total number of recorded increments exactly, and no sanitizer reports a race

### Requirement: Counter serialization contract

`Counter` SHALL provide a cold-path `std::string to_string() const` and free `to_string(const T&)`, a diagnostic `to_json` under `JSON_NOEXCEPTION=1`, and `std::formatter` support; it SHALL NOT provide `from_json`, preserving the Meter-only creation rule.

#### Scenario: Counter diagnostics render

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a counter holding counts
- **THEN** the member and free results are equal and non-empty, they include the count and policy names, and the formatter renders the same text

#### Scenario: Counter JSON is write-only

- **WHEN** `to_json` serializes a counter under `JSON_NOEXCEPTION=1` and a translation unit attempts `from_json`
- **THEN** the JSON describes the counter's state, and the `from_json` attempt fails at compile time

### Requirement: Gauge recording surface

`Gauge<ValueType, ConcurrencyPolicy, AccessPolicy, ResetPolicy>` SHALL support `set(ValueType) noexcept`, `add(ValueType) noexcept`, and `sub(ValueType) noexcept` for `std::int64_t` and `double` instantiations. `SingleThreaded` mode SHALL use plain storage; `MultiThreaded` mode SHALL use the per-thread shard scheme with `set` moving the aggregate to the target value as observed at call time.

#### Scenario: Single-threaded gauge operations are exact

- **WHEN** a single-threaded gauge executes `set(10)`, `add(5)`, and `sub(3)`
- **THEN** the gauge reads `12` for both `std::int64_t` and `double` instantiations

#### Scenario: Multi-threaded gauge changes are exact

- **WHEN** 32 threads each perform one million `add(1)` calls on one `MultiThreaded` gauge
- **THEN** the aggregate is exactly 32 million

#### Scenario: Set establishes the observed aggregate

- **WHEN** a single writer calls `set(42)` on an otherwise idle `MultiThreaded` gauge and the gauge is read
- **THEN** the aggregate equals `42`

#### Scenario: Gauge hot-path latency gate

- **WHEN** the Release latency benchmark executes `set` and `add` on a gauge
- **THEN** the median per-call latency stays at or below the documented Release gate for gauges

### Requirement: Gauge reset windows

With `ResetPolicy::Cumulative`, a gauge SHALL retain its aggregate across reads and harvests. With `ResetPolicy::ResetOnRead`, reads and harvests SHALL report only the net activity of the current window and reset it, in both concurrency modes.

#### Scenario: Cumulative gauge retains its value

- **WHEN** a cumulative gauge is set and then read and harvested repeatedly
- **THEN** every observation reports the set aggregate until the next `set`, `add`, or `sub`

#### Scenario: Reset-on-read gauge reports window activity

- **WHEN** a reset-on-read gauge receives `add(5)`, is read, receives `add(3)`, and is read again
- **THEN** the reads return `5` and `3`

#### Scenario: Concurrent window records land once

- **WHEN** recording threads run concurrently with periodic harvests on a `MultiThreaded` reset-on-read gauge
- **THEN** the per-window sums add up to the total recorded delta exactly and no sanitizer reports a race

### Requirement: Gauge serialization contract

`Gauge` SHALL provide a cold-path `std::string to_string() const` and free `to_string(const T&)`, a diagnostic `to_json` under `JSON_NOEXCEPTION=1`, and `std::formatter` support; it SHALL NOT provide `from_json`, preserving the Meter-only creation rule.

#### Scenario: Gauge diagnostics render

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a gauge holding a value
- **THEN** the member and free results are equal and non-empty, they include the value, value type, and policy names, and the formatter renders the same text

#### Scenario: Gauge JSON is write-only

- **WHEN** `to_json` serializes a gauge under `JSON_NOEXCEPTION=1` and a translation unit attempts `from_json`
- **THEN** the JSON describes the gauge's state, and the `from_json` attempt fails at compile time
