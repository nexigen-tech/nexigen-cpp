# Spec Delta

## ADDED Requirements

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
