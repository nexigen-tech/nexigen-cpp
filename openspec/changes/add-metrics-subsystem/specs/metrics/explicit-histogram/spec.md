# Spec Delta

## Purpose

Defines the `ExplicitBucketHistogram` instrument: caller-supplied bucket upper bounds stored in a fixed inline array, exact bucket accounting with count/sum/min/max, and percentile snapshots derived from the buckets.

## ADDED Requirements

### Requirement: Explicit bucket configuration

`ExplicitBucketHistogram` SHALL be created through `Meter` with strictly increasing finite upper bounds stored in a fixed inline array (at most 64 bounds). Bucket 0 covers values up to its bound, each later bucket covers values up to its own bound, and values above the last bound land in a final overflow bucket.

#### Scenario: Stored bounds are reflected in snapshots

- **WHEN** a histogram is created with bounds 1, 2, 4 and a snapshot is taken
- **THEN** the snapshot reports those bounds and one bucket per bound plus the overflow bucket

#### Scenario: Bucket assignment semantics

- **WHEN** values 1, 1.5, 4, and 100 are recorded against bounds 1, 2, 4
- **THEN** they land in buckets for bound 1, bound 2, bound 4, and the overflow bucket respectively

#### Scenario: Bounds are immutable after creation

- **WHEN** recording continues after creation
- **THEN** the bucket layout observed in snapshots never changes

### Requirement: Explicit histogram recording and aggregation

`record(double) noexcept` SHALL increment exactly one bucket and maintain `count`, `sum`, `min`, and `max` with zero heap allocation and bounded comparison work over the inline bounds. Non-finite samples SHALL leave the buckets untouched and increment the ignored-sample counter.

#### Scenario: Summary aggregates recorded values

- **WHEN** values 1.5, 2.5, and 3.5 are recorded
- **THEN** `count` is 3, `sum` is 7.5, `min` is 1.5, `max` is 3.5, and the bucket counts match the assignment rule

#### Scenario: Non-finite samples are ignored visibly

- **WHEN** NaN or an infinity is recorded
- **THEN** no bucket, count, sum, min, or max changes and the ignored-sample counter increases by one

#### Scenario: Recording latency gate

- **WHEN** the Release latency benchmark executes a sustained `record(double)` loop against a populated histogram
- **THEN** the median per-call latency stays at or below the documented Release gate for explicit histograms

### Requirement: Explicit histogram percentile snapshots

Snapshots SHALL report `p50`, `p90`, `p99`, and `p99.9` computed from bucket counts with linear interpolation between the containing bucket's lower and upper bounds. Percentiles SHALL be monotonically non-decreasing in rank and all zero over an empty histogram.

#### Scenario: Uniform distribution percentiles are accurate

- **WHEN** uniformly spaced values spanning bounds 1, 2, 4, 8 are recorded and percentiles are taken
- **THEN** each reported percentile lies within one bucket width of the exact quantile

#### Scenario: Percentiles are ordered

- **WHEN** any recorded distribution is snapshotted
- **THEN** `p50 <= p90 <= p99 <= p99.9` within floating-point tolerance

#### Scenario: Empty histogram reports zeros

- **WHEN** percentiles are taken before any recording
- **THEN** all four percentiles are 0 and `count` is 0

### Requirement: Explicit histogram read surface

An instrument instantiated with `AccessPolicy::Readable` SHALL expose `read()` returning its `HistogramSnapshot` (bounds, bucket counts, count/sum/min/max, percentiles, ignored samples) and `snapshot()` returning the full `MetricRecord`, both const-callable and allocation-free.

#### Scenario: Read returns the live histogram snapshot

- **WHEN** a readable explicit histogram holds samples and `read()` is called
- **THEN** the returned `HistogramSnapshot` carries the bounds, bucket counts, summary, and percentiles

#### Scenario: Snapshot returns the full record

- **WHEN** `snapshot()` is called on the same instrument
- **THEN** the returned `MetricRecord` carries `MetricKind::ExplicitHistogram`, the meter and metric identity, the policies, and the histogram values

### Requirement: Explicit histogram reset windows

With `ResetPolicy::Cumulative`, bucket counts and summary SHALL persist across reads and harvests. With `ResetPolicy::ResetOnRead`, each observation SHALL report only the samples recorded since the previous one and reset the window, in both concurrency modes.

#### Scenario: Cumulative buckets persist

- **WHEN** a cumulative histogram is read and harvested repeatedly after recording
- **THEN** every observation reports all samples recorded so far

#### Scenario: Reset-on-read reports window samples

- **WHEN** a reset-on-read histogram records values, is harvested, records more, and is harvested again
- **THEN** each batch carries only its own window's buckets and summary

#### Scenario: Records racing a reset land once

- **WHEN** recording threads run concurrently with periodic harvests on a `MultiThreaded` reset-on-read histogram
- **THEN** the per-window counts sum to the total number of recorded samples exactly and no sanitizer reports a race

### Requirement: Multi-threaded explicit histogram recording

`MultiThreaded` histograms SHALL accumulate in `alignas(64)` per-thread shards merged at observation time, with up to 32 `thread_local`-registered threads and a shared fallback shard beyond that, keeping totals exact under any thread count.

#### Scenario: Concurrent recording is exact

- **WHEN** 32 threads each record one million samples into one histogram
- **THEN** the merged count is exactly 32 million and every bucket total matches the expected distribution

#### Scenario: Threads beyond the shard pool stay exact

- **WHEN** more than 32 threads record into the same histogram
- **THEN** the cumulative totals remain exact through the shared fallback shard

### Requirement: Explicit histogram serialization contract

`ExplicitBucketHistogram` and `HistogramSnapshot` SHALL each provide `to_string() const`, a free `to_string(const T&)`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter` support; the instrument itself SHALL NOT provide `from_json`, preserving the Meter-only creation rule.

#### Scenario: Histogram snapshot serialization round-trips

- **WHEN** a `HistogramSnapshot` holding bounds, bucket counts, and percentiles is serialized and parsed back
- **THEN** every field survives the round-trip under `JSON_NOEXCEPTION=1`

#### Scenario: Diagnostics render

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to the instrument and its snapshot
- **THEN** member and free results agree, they include the bounds and summary, and the formatter renders the same text

#### Scenario: Instrument JSON is write-only

- **WHEN** `to_json` serializes the instrument under `JSON_NOEXCEPTION=1` and a translation unit attempts `from_json` into it
- **THEN** the JSON describes its state and the `from_json` attempt fails at compile time
