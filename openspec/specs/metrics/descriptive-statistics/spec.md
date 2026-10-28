# metrics/descriptive-statistics Specification

## Purpose

Defines the `DescriptiveStatistics` instrument: allocation-free online `count`, `min`, `max`, `mean`, and `std` in O(1) per sample, with cumulative and reset-on-read windows that never block or lose concurrent recordings.

## Requirements

### Requirement: Online moment statistics

`DescriptiveStatistics<ConcurrencyPolicy, AccessPolicy, ResetPolicy>` SHALL accept `record(double) noexcept` and maintain `count`, `min`, `max`, `mean`, and `std` in O(1) time per sample with zero heap allocation, using Welford-style numerically stable moments. `std` SHALL be the population standard deviation, `0.0` for a single sample, and `0.0` over zero samples with all summary fields zeroed.

#### Scenario: Known sequence produces exact summary

- **WHEN** the sequence 2, 4, 4, 4, 5, 5, 7, 9 is recorded
- **THEN** `count` is 8, `min` is 2, `max` is 9, `mean` is 5, and `std` is 2 within floating-point tolerance

#### Scenario: Single sample has zero spread

- **WHEN** one value is recorded and the summary is read
- **THEN** `count` is 1, `min`, `max`, and `mean` equal that value, and `std` is 0

#### Scenario: Empty statistics are well defined

- **WHEN** a summary is read before any recording
- **THEN** `count` is 0 and `min`, `max`, `mean`, and `std` are 0

#### Scenario: Non-finite samples are ignored visibly

- **WHEN** NaN or an infinity is recorded
- **THEN** the moments are unchanged and the summary's ignored-sample counter increases by one

#### Scenario: Recording latency gate

- **WHEN** the Release latency benchmark executes a sustained `record(double)` loop
- **THEN** the median per-call latency stays at or below the documented Release gate for statistics recording

### Requirement: Readable statistics surface

An instrument instantiated with `AccessPolicy::Readable` SHALL expose `read()` returning the current `StatisticsSnapshot` and `snapshot()` returning the full `MetricRecord`, both const-callable and allocation-free.

#### Scenario: Read returns the live summary

- **WHEN** a readable statistics instrument holds samples and `read()` is called
- **THEN** the returned `StatisticsSnapshot` carries the current count, min, max, mean, and std

#### Scenario: Snapshot returns the full record

- **WHEN** `snapshot()` is called on the same instrument
- **THEN** the returned `MetricRecord` carries `MetricKind::DescriptiveStatistics`, the meter and metric identity, the policies, and the summary values

### Requirement: Cumulative statistics window

With `ResetPolicy::Cumulative`, statistics SHALL accumulate over all recorded samples for the instrument's lifetime; reads and harvests SHALL leave the moments intact.

#### Scenario: Moments accumulate across reads

- **WHEN** samples are recorded in three phases with reads and harvests between them
- **THEN** every observation reflects all samples recorded so far

#### Scenario: Harvest does not reset cumulative moments

- **WHEN** the background harvester collects a cumulative statistics instrument twice without new recordings
- **THEN** both batches report identical moments

### Requirement: Reset-on-read statistics windows

With `ResetPolicy::ResetOnRead`, `read()`, `snapshot()`, and harvesting SHALL report exactly the samples recorded since the previous observation and reset the window. In `MultiThreaded` mode the instrument SHALL use wait-free double-buffered active/shadow banks per thread so the swap resets the window without blocking recording threads and without losing concurrent `record(double)` calls.

#### Scenario: Window contains exactly the interval's samples

- **WHEN** samples are recorded, the window is harvested, more samples are recorded, and the window is harvested again
- **THEN** the first summary covers only the first phase and the second only the second phase, with exact counts and means

#### Scenario: Records racing a swap land exactly once

- **WHEN** recording threads run concurrently with periodic harvests on a `MultiThreaded` reset-on-read instrument
- **THEN** the per-window counts sum to the total number of `record()` calls exactly and no sanitizer reports a race

#### Scenario: Swapping never blocks recording

- **WHEN** harvesting runs repeatedly while a thread records in a tight loop
- **THEN** every `record()` call completes in bounded time with no waiting on the harvester

### Requirement: Multi-threaded statistics merging

`MultiThreaded` statistics SHALL accumulate in `alignas(64)` per-thread slots merged into one summary at observation time. Up to 32 threads SHALL hold exclusive `thread_local`-registered slots; a record from a further thread SHALL be dropped and counted in the snapshot's dropped-sample counter so no loss is silent.

#### Scenario: Sharded summary matches the single-threaded reference

- **WHEN** 32 threads each record a distinct known value set into one `MultiThreaded` instrument
- **THEN** the merged count, min, max, and mean exactly match the single-threaded reference and `std` matches within floating-point tolerance

#### Scenario: Slot exhaustion is visible

- **WHEN** more than 32 threads record into the same instrument
- **THEN** the dropped-sample counter reports every record from threads without exclusive slots, and the accepted samples' moments stay exact

### Requirement: Statistics serialization contract

`DescriptiveStatistics` and `StatisticsSnapshot` SHALL each provide `to_string() const`, a free `to_string(const T&)`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter` support; the instrument itself SHALL NOT provide `from_json`, preserving the Meter-only creation rule.

#### Scenario: Summary serialization round-trips

- **WHEN** a `StatisticsSnapshot` holding moments is serialized and parsed back
- **THEN** every field survives the round-trip under `JSON_NOEXCEPTION=1`

#### Scenario: Diagnostics render

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a statistics instrument and its snapshot
- **THEN** member and free results agree, they include the moments and policy names, and the formatter renders the same text

#### Scenario: Instrument JSON is write-only

- **WHEN** `to_json` serializes the instrument under `JSON_NOEXCEPTION=1` and a translation unit attempts `from_json` into it
- **THEN** the JSON describes its state and the `from_json` attempt fails at compile time
