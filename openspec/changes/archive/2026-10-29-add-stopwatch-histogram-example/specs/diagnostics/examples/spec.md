# Spec Delta

## ADDED Requirements

### Requirement: ex_stopwatch_histogram records measured scopes into a histogram

The example SHALL measure per-tick work with a `ScopedStopwatch` bound to a calibrated `nexigen::chrono::Stamp_source` and record each scope's elapsed nanoseconds into an `ExplicitBucketHistogram` created through the `MetricProvider`/`Meter` factory, with bucket bounds derived from light/heavy probe measurements so the distribution is usable in every build mode.

#### Scenario: Every tick produces exactly one histogram sample

- **WHEN** the example completes its measurement loop
- **THEN** the histogram count equals the number of measured ticks, no sample is rejected as non-finite, and the bucket counts sum to the total

#### Scenario: Bucket bounds reflect the measured envelope

- **WHEN** the histogram is created after the probe phase
- **THEN** the bounds are finite, positive and strictly increasing, and the snapshot reports them as its bucket edges

### Requirement: ex_stopwatch_histogram renders the histogram on the console

The example SHALL print count, sum, min, max, `p50`/`p90`/`p99`/`p99.9`, a per-bucket table with ASCII bars, and the cold-path `HistogramSnapshot::to_string()` diagnostic to stdout, and SHALL self-verify the snapshot (ordered percentiles, populated percentiles, samples fitting inside a whole-run `Stopwatch` interval) before exiting.

#### Scenario: Console report shows summary and bucket distribution

- **WHEN** the example prints its report
- **THEN** the summary values and every bucket row appear on stdout, and the percentiles satisfy `p50 <= p90 <= p99 <= p99.9`

#### Scenario: Failed verification is visible

- **WHEN** any self-check fails
- **THEN** the example reports the failure on stderr and exits with a non-zero status

### Requirement: ex_stopwatch_histogram is buildable, runnable, and a CTest smoke test

`ex_stopwatch_histogram` SHALL be a self-contained executable under `examples/diagnostics/`, registered through the shared `nexigen_add_example` helper against the `nexigen::diagnostics` and `nexigen::metrics` targets, exposed as a CTest entry with the `examples` label and the shared timeout, exiting 0 on success, and passing under the project's Debug, Release, ThreadSanitizer, and AddressSanitizer configurations.

#### Scenario: Example runs to exit zero

- **WHEN** `ex_stopwatch_histogram` is built and launched directly (or via CTest) without arguments
- **THEN** it completes its demonstration, exits with status 0, and emits no sanitizer diagnostics in any build mode

#### Scenario: Example seeds its own data and needs no harness

- **WHEN** the example is run outside any test runner
- **THEN** it creates its own clock, provider, workloads and bucket bounds, requires no fixture or pre-existing files, and prints its report to stdout
