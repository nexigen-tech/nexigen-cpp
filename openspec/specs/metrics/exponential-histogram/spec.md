# metrics/exponential-histogram Specification

## Purpose

Defines the `ExponentialHistogram` instrument: automatic base-2 exponential bucket intervals from a configurable scale, O(1) bitwise bucket mapping without `std::log`, bounded inline storage with loss-free coarsening, and percentile snapshots.

## Requirements

### Requirement: Exponential scale configuration

`ExponentialHistogram` SHALL be created through `Meter` with a configurable `scale`, where the nominal bucket ratio is base = 2^(2^-scale), supported for `0 <= scale <= 20`. The configured scale SHALL be reported in every snapshot, and out-of-range scales SHALL be rejected at creation with `core::ErrorCode::InvalidArgument`.

#### Scenario: Valid scale is accepted and reported

- **WHEN** a histogram is created with scale 10 and snapshotted
- **THEN** creation succeeds and the snapshot reports the configured scale

#### Scenario: Out-of-range scale is rejected

- **WHEN** creation is attempted with a negative scale or a scale above 20
- **THEN** the call returns `core::ErrorCode::InvalidArgument` and registers no instrument

### Requirement: Bitwise bucket index mapping

Recording a positive finite value SHALL map it to its bucket index in O(1) bounded work by extracting the IEEE-754 exponent and mantissa with `std::bit_cast<std::uint64_t>`, without calling `std::log`, `std::log2`, or allocating. The mapping SHALL be deterministic and monotonic: `v1 < v2` implies `index(v1) <= index(v2)`, and bucket `i` covers `[base^i, base^(i+1))`.

#### Scenario: Bucket containment holds across the double domain

- **WHEN** boundary values, values one ULP around every boundary, and pseudorandom positive doubles are mapped at several scales
- **THEN** every value lands in the bucket `i` satisfying `base^i <= v < base^(i+1)`, verified against reference boundaries computed in the test

#### Scenario: Exact powers of two map exactly

- **WHEN** values 2^-10, 1.0, and 2^10 are mapped at any supported scale
- **THEN** each lands exactly in the bucket whose lower bound equals the value

#### Scenario: Mapping is deterministic and monotonic

- **WHEN** an increasing sweep of positive doubles is mapped twice
- **THEN** both index sequences are identical and non-decreasing

#### Scenario: Mapping latency gate

- **WHEN** the Release latency benchmark executes a sustained `record(double)` loop
- **THEN** the median per-call latency stays at or below the documented Release gate for exponential histograms

### Requirement: Value domain handling

`record(double) noexcept` SHALL accept positive finite values into buckets. Positive and negative zeros SHALL increment a zero counter that ranks below all positive buckets in percentiles. Negative and non-finite values SHALL leave buckets untouched and increment the ignored-sample counter.

#### Scenario: Zeros are counted separately

- **WHEN** `+0.0` and `-0.0` are recorded
- **THEN** the zero counter increases by two and no bucket changes

#### Scenario: Negative and non-finite samples are ignored visibly

- **WHEN** a negative value, NaN, or an infinity is recorded
- **THEN** no bucket or summary field changes and the ignored-sample counter increases by one

### Requirement: Bounded bucket storage and coarsening

Bucket counts SHALL live in a fixed inline array per recording slot. When the observed value range cannot fit the inline buckets at the current resolution, the instrument SHALL coarsen its effective scale downward so every sample remains counted exactly once, and SHALL report the effective scale in snapshots.

#### Scenario: Wide ranges coarsen without losing samples

- **WHEN** values spanning many orders of magnitude overflow the inline bucket window
- **THEN** the total across buckets plus the zero counter equals the accepted sample count exactly and the effective scale is at most the configured scale

#### Scenario: Coarsened buckets keep containment semantics

- **WHEN** a snapshot is taken after coarsening
- **THEN** its reported bucket edges still satisfy `base^i <= v < base^(i+1)` at the effective scale

### Requirement: Exponential histogram percentile snapshots

Snapshots SHALL report `p50`, `p90`, `p99`, and `p99.9` from bucket counts over positive samples plus zeros, using each bucket's representative value. Reported percentiles SHALL be ordered and within one bucket width of the exact quantile at the effective scale, and all zero over an empty histogram.

#### Scenario: Log-uniform distribution percentiles are accurate

- **WHEN** samples drawn log-uniformly over several orders of magnitude are recorded
- **THEN** each reported percentile is within one bucket width of the exact quantile

#### Scenario: Percentiles are ordered

- **WHEN** any recorded distribution is snapshotted
- **THEN** `p50 <= p90 <= p99 <= p99.9` within floating-point tolerance

#### Scenario: Empty histogram reports zeros

- **WHEN** percentiles are taken before any recording
- **THEN** all four percentiles are 0 and `count` is 0

### Requirement: Exponential histogram read surface

An instrument instantiated with `AccessPolicy::Readable` SHALL expose `read()` returning its `HistogramSnapshot` (effective scale, bucket counts, count/sum/min/max, zeros, percentiles, ignored samples) and `snapshot()` returning the full `MetricRecord`, both const-callable and allocation-free.

#### Scenario: Read returns the live histogram snapshot

- **WHEN** a readable exponential histogram holds samples and `read()` is called
- **THEN** the returned `HistogramSnapshot` carries the effective scale, bucket counts, summary, zero count, and percentiles

#### Scenario: Snapshot returns the full record

- **WHEN** `snapshot()` is called on the same instrument
- **THEN** the returned `MetricRecord` carries `MetricKind::ExponentialHistogram`, the meter and metric identity, the policies, and the histogram values

### Requirement: Exponential histogram reset windows

With `ResetPolicy::Cumulative`, bucket counts and summary SHALL persist across reads and harvests. With `ResetPolicy::ResetOnRead`, each observation SHALL report only the samples recorded since the previous one and reset the window, in both concurrency modes.

#### Scenario: Cumulative buckets persist

- **WHEN** a cumulative histogram is read and harvested repeatedly after recording
- **THEN** every observation reports all samples recorded so far

#### Scenario: Reset-on-read reports window samples

- **WHEN** a reset-on-read histogram records values, is harvested, records more, and is harvested again
- **THEN** each batch carries only its own window's buckets and summary

#### Scenario: Records racing a reset land once

- **WHEN** recording threads run concurrently with periodic harvests on a `MultiThreaded` reset-on-read histogram
- **THEN** the per-window counts sum to the total number of accepted samples exactly and no sanitizer reports a race

### Requirement: Multi-threaded exponential histogram recording

`MultiThreaded` histograms SHALL accumulate in `alignas(64)` per-thread shards merged at observation time, with up to 32 `thread_local`-registered threads and a shared fallback shard beyond that, keeping totals exact under any thread count.

#### Scenario: Concurrent recording is exact

- **WHEN** 32 threads each record one million samples into one histogram
- **THEN** the merged count is exactly 32 million and every bucket total matches the expected distribution

#### Scenario: Threads beyond the shard pool stay exact

- **WHEN** more than 32 threads record into the same histogram
- **THEN** the cumulative totals remain exact through the shared fallback shard

### Requirement: Exponential histogram serialization contract

`ExponentialHistogram` and `HistogramSnapshot` SHALL each provide `to_string() const`, a free `to_string(const T&)`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter` support; the instrument itself SHALL NOT provide `from_json`, preserving the Meter-only creation rule.

#### Scenario: Histogram snapshot serialization round-trips

- **WHEN** a `HistogramSnapshot` holding effective scale, bucket counts, and percentiles is serialized and parsed back
- **THEN** every field survives the round-trip under `JSON_NOEXCEPTION=1`

#### Scenario: Diagnostics render

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to the instrument and its snapshot
- **THEN** member and free results agree, they include the effective scale and summary, and the formatter renders the same text

#### Scenario: Instrument JSON is write-only

- **WHEN** `to_json` serializes the instrument under `JSON_NOEXCEPTION=1` and a translation unit attempts `from_json` into it
- **THEN** the JSON describes its state and the `from_json` attempt fails at compile time
