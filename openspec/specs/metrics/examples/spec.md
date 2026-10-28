# metrics/examples Specification

## Purpose

Defines the runnable `nexigen::metrics` example executables and the observable behavior each one demonstrates, so integrators can copy verified usage patterns for instruments, policies, windows, and the provider/sink pipeline.

## Requirements

### Requirement: Metrics example targets

The project SHALL provide four self-contained executables under `examples/metrics/` — `ex_counters_and_gauges`, `ex_descriptive_statistics`, `ex_histograms`, and `ex_metric_provider_and_sinks` — each with its own `main`, no test-harness dependency, registered as one CTest smoke test named after the executable and labeled `examples`, exiting `0` on success and non-zero when its self-verification fails.

#### Scenario: Targets are registered and labeled

- **WHEN** a default build completes and `ctest` is run
- **THEN** four tests named after the executables run under the `examples` label and pass

#### Scenario: Failure is visible

- **WHEN** an example's self-verification fails
- **THEN** it reports the failure on an appropriate stream and exits with a non-zero status

### Requirement: Counter and gauge example

`ex_counters_and_gauges` SHALL demonstrate `MetricProvider`/`Meter` creation; `SingleThreaded` and `MultiThreaded` counters and `std::int64_t`/`double` gauges; `AccessPolicy::Readable` instruments read via `read()`/`snapshot()` versus `AccessPolicy::WriteOnly` instruments updated by multiple threads and harvested by the provider; `Cumulative` versus `ResetOnRead` windows; and cold-path `to_string()` diagnostics.

#### Scenario: Single-threaded and multi-threaded accumulation

- **WHEN** single-threaded counter and gauge operations and multi-threaded worker threads complete
- **THEN** the printed aggregates exactly match the expected totals for both value types

#### Scenario: Readable versus write-only access

- **WHEN** a `Readable` instrument is read in application code while a `WriteOnly` instrument is only updated by workers
- **THEN** `read()`/`snapshot()` output is shown for the `Readable` instrument and the `WriteOnly` instrument's values appear only in provider harvest output

#### Scenario: Reset policies are contrasted

- **WHEN** `Cumulative` and `ResetOnRead` instruments are polled repeatedly between recorded batches
- **THEN** `Cumulative` totals persist across polls and `ResetOnRead` reports only each poll's delta

#### Scenario: Diagnostics render

- **WHEN** `to_string()` is applied to instruments and snapshots
- **THEN** non-empty diagnostics containing the values and policy names are printed

### Requirement: Descriptive statistics example

`ex_descriptive_statistics` SHALL simulate a real-time sensor/control loop recording measurements into `DescriptiveStatistics`, demonstrate `Cumulative` all-time statistics and `ResetOnRead` windowed statistics polled between ticks with multiple producer threads, and print snapshots via `to_string()` and `std::format` with policy enums.

#### Scenario: Running summary reflects the stream

- **WHEN** a stream of measurements is recorded and the summary is read
- **THEN** the printed count, min, max, mean, and std reflect the recorded stream

#### Scenario: Windowed statistics isolate intervals

- **WHEN** multiple producer threads record concurrently while `ResetOnRead` windows are polled between ticks and during recording
- **THEN** tick-boundary polls report exactly their interval's samples, every record is counted in exactly one window (a record racing a poll boundary may land in a later window), and the `Cumulative` instrument reports all samples

#### Scenario: Formatted diagnostics agree

- **WHEN** `to_string()` and `std::format` with policy enums render the same snapshot
- **THEN** both render equal, non-empty summary text

### Requirement: Histogram example

`ex_histograms` SHALL demonstrate an `ExplicitBucketHistogram` with caller-defined latency upper bounds and an `ExponentialHistogram` with automatic base-2 exponential bucket mapping across wide dynamic ranges, measuring simulated task durations from multiple threads through `nexigen::chrono` stamping, and print `count`, `sum`, `min`, `max`, and `p50`/`p90`/`p99`/`p99.9`.

#### Scenario: Explicit bounds are reflected

- **WHEN** measured durations are recorded into the explicit histogram and it is read
- **THEN** its snapshot reports the configured bounds and matching bucket counts

#### Scenario: Exponential histogram covers wide ranges

- **WHEN** durations spanning several orders of magnitude are recorded into the exponential histogram
- **THEN** the reported totals account for every accepted sample and the effective scale is reported

#### Scenario: Percentiles are reported and ordered

- **WHEN** either histogram is read after recording
- **THEN** count, sum, min, max and all four percentiles are printed, and `p50 <= p90 <= p99 <= p99.9`

### Requirement: Provider and sink pipeline example

`ex_metric_provider_and_sinks` SHALL demonstrate an end-to-end HIL telemetry pipeline combining a `MetricProvider` with periodic background collection and CPU-affinity pinning, a `CompositeMetricSink<ConsoleMetricSink, JsonFileMetricSink, NullMetricSink>` constructed with configured members, concurrent producers over every instrument kind, and cold-path read-back verification of the emitted JSONL telemetry.

#### Scenario: Composite accepts configured members

- **WHEN** the composite containing a path-configured `JsonFileMetricSink` is constructed
- **THEN** construction succeeds and every member receives each batch exactly once

#### Scenario: Background harvesting honors interval and affinity

- **WHEN** the provider runs with a periodic `collect_interval` (e.g. 50 ms) and a non-zero `core_mask`
- **THEN** batches arrive periodically, background sink callbacks run on the harvester, and `startup_status()` reports pinning success

#### Scenario: JSONL telemetry is written and read back

- **WHEN** `flush()` completes and the per-run JSONL file under `/tmp/nexigen-examples-metrics/` is read back line by line
- **THEN** every line parses as one JSON batch object carrying an ISO-8601 UTC timestamp and the recorded metrics

#### Scenario: Pipeline verification decides the exit status

- **WHEN** read-back verification succeeds
- **THEN** the example exits `0`; if any batch line fails to parse, it exits non-zero
