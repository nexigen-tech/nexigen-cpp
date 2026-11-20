# core/meter-concepts Specification

## Purpose
Defines the meter and instrument concepts that every telemetry module uses to create optional
instrumentation, plus the no-op default that keeps an uninstrumented build free of counter code.

## Requirements

### Requirement: Diagnosable value

`core::concepts::Diagnosable<T>` SHALL require a cold-path `to_string() const` returning a value
convertible to `std::string`. Statistics and histogram snapshots SHALL satisfy it so instrument
concepts can be expressed without naming metrics snapshot types.

#### Scenario: Metrics snapshot satisfies the concept

- **WHEN** `Diagnosable` is checked against `MetricRecord`, `StatisticsSnapshot`, and
  `HistogramSnapshot`
- **THEN** each is satisfied

#### Scenario: Type without rendering is rejected

- **WHEN** a type has no `to_string()`
- **THEN** `Diagnosable` is false

### Requirement: Counter concept

`core::CounterLike` SHALL be satisfied by a type exposing `add(std::uint64_t) noexcept` and
`inc() noexcept`. The concept SHALL NOT require any other metric capability, so a counter
implementation stays independent of gauges, histograms, and provider policy.

#### Scenario: Metric counter satisfies the concept

- **WHEN** `core::CounterLike` is checked against a `metrics::Counter` instantiation
- **THEN** the concept is satisfied and recording uses `add`/`inc`

#### Scenario: Incomplete type is rejected

- **WHEN** a type lacks `add` or `inc`, or declares them without `noexcept`
- **THEN** `core::CounterLike` is not satisfied and the endpoint does not compile

### Requirement: Meter concept

`core::MeterLike` SHALL be satisfied by a factory whose `create_counter(name, unit)` returns a
`core::Result`-like value holding a pointer to a `core::CounterLike`. The concept SHALL NOT name
`nexigen::metrics`, so any module can accept a meter without depending on the metrics module.

#### Scenario: Metric meter satisfies the concept

- **WHEN** `core::MeterLike` is checked against `metrics::Meter`
- **THEN** the concept is satisfied and `create_counter` yields a usable counter

#### Scenario: Factory error is propagated

- **WHEN** `create_counter` rejects a duplicate or over-long name
- **THEN** the endpoint reports `InvalidArgument` and stays usable

#### Scenario: Module without metrics compiles

- **WHEN** a module that does not link `nexigen::metrics` includes the concept header
- **THEN** it compiles and no metrics symbol is referenced

### Requirement: Null meter default

`core::NullMeter` SHALL provide a counter whose `add` and `inc` do nothing and a
`create_counter` that always succeeds. It SHALL satisfy `core::MeterLike` and
`core::CounterLike`, hold no mutable state, and be usable as the default meter type wherever a
meter-aware template omits its meter.

#### Scenario: Null meter satisfies the concepts

- **WHEN** `MeterLike<NullMeter>` and `CounterLike<NullMeter::Counter>` are checked
- **THEN** both are true at compile time

#### Scenario: Null counter is a no-op

- **WHEN** the counter is obtained and `add`/`inc` are called
- **THEN** creation succeeds and no state changes

#### Scenario: Null meter is accepted everywhere

- **WHEN** a template constrained by `core::MeterLike` is instantiated with `core::NullMeter`
- **THEN** it compiles and every counter call is a no-op

### Requirement: Gauge concept

`core::GaugeLike<G, Value>` SHALL require `set(Value) noexcept`, `add(Value) noexcept`, and
`sub(Value) noexcept`. Reading SHALL NOT be required, because the metrics module supports
write-only instruments.

#### Scenario: Metric gauge satisfies the concept

- **WHEN** `core::GaugeLike` is checked against `metrics::Gauge<std::int64_t>`
- **THEN** the concept is satisfied

#### Scenario: Missing subtraction is rejected

- **WHEN** a gauge type lacks `sub`
- **THEN** `core::GaugeLike` is false

### Requirement: State gauge concept

`core::StateGaugeLike<S>` SHALL require `set(std::string_view) noexcept` returning `bool`, so a
named state can replace the current value and report rejection without throwing.

#### Scenario: Metric state gauge satisfies the concept

- **WHEN** `core::StateGaugeLike` is checked against `metrics::StateGauge`
- **THEN** the concept is satisfied

#### Scenario: Non-boolean set is rejected

- **WHEN** a state gauge declares `set` returning `void`
- **THEN** `core::StateGaugeLike` is false

### Requirement: Descriptive statistics concept

`core::DescriptiveStatisticsLike<D>` SHALL require `record(double) noexcept`, `read()` and
`snapshot()` returning diagnosable values. It SHALL NOT name the metrics snapshot types.

#### Scenario: Metric statistics satisfy the concept

- **WHEN** the concept is checked against `metrics::DescriptiveStatistics`
- **THEN** the concept is satisfied

#### Scenario: Non-diagnosable snapshot is rejected

- **WHEN** `snapshot()` returns a type without `to_string()`
- **THEN** the concept is false

### Requirement: Histogram concept

`core::HistogramLike<H>` SHALL require `record(double) noexcept`, `read()` and `snapshot()`
returning diagnosable values, and SHALL be shared by explicit-bucket and exponential histograms.

#### Scenario: Both histogram kinds satisfy the concept

- **WHEN** the concept is checked against `ExplicitBucketHistogram` and `ExponentialHistogram`
- **THEN** both are satisfied

#### Scenario: Missing record is rejected

- **WHEN** a histogram type lacks `record`
- **THEN** the concept is false

### Requirement: Instrument meter concept

`core::InstrumentMeterLike<M>` SHALL extend `core::MeterLike<M>` with the creation entry points for
every instrument kind: gauge, state gauge, descriptive statistics, explicit histogram, and
exponential histogram. Each created type SHALL satisfy its instrument concept.

#### Scenario: Metrics meter creates every instrument

- **WHEN** `core::InstrumentMeterLike` is checked against `metrics::Meter`
- **THEN** the concept is satisfied

#### Scenario: Counter-only meter is insufficient

- **WHEN** a factory exposes only `create_counter`
- **THEN** `core::InstrumentMeterLike` is false while `core::MeterLike` remains true
