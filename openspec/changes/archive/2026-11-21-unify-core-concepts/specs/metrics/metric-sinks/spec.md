# Spec Delta

## MODIFIED Requirements

### Requirement: Metric sink concept

`nexigen::metrics::concepts::MetricSink` SHALL be a C++23 concept satisfied only by types satisfying `core::concepts::Sink<T, MetricBatchSnapshot>`: `on_event(const MetricBatchSnapshot&) noexcept -> core::Status` and `flush() noexcept -> core::Status`, with no virtual functions required.

#### Scenario: Conforming sinks satisfy the concept

- **WHEN** `concepts::MetricSink<T>` is evaluated for `NullMetricSink`, `ConsoleMetricSink`, `JsonFileMetricSink`, and a `CompositeMetricSink` over them
- **THEN** it is `true` in a constant expression

#### Scenario: Missing or wrong member rejects the concept

- **WHEN** a type lacks `on_event`, lacks `flush`, or returns a non-`core::Status` type from either
- **THEN** `concepts::MetricSink<T>` is `false`

#### Scenario: Sink callbacks are allocation-free

- **WHEN** any sink processes a batch under an armed allocation arena
- **THEN** no `operator new`, `delete`, `malloc`, `free`, or dynamic container growth occurs

#### Scenario: Old delivery name is rejected

- **WHEN** a type still exposes `on_batch`
- **THEN** `concepts::MetricSink<T>` is `false`
