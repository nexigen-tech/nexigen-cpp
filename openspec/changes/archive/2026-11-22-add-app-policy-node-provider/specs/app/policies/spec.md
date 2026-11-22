# Spec Delta

## MODIFIED Requirements

### Requirement: Iceoryx2 policy publishes all telemetry as fixed frames

`Iceoryx2Policy<A,B,C,D,NodeProvider>` SHALL provide `Iceoryx2Logger`, `Iceoryx2MetricSink` and `Iceoryx2TraceSink`, use `Nexigen/Log/nexigen`, `nexigen/metrics/stream` and `nexigen/trace/stream`, and stamp the four tag characters into every frame. The metric and trace sinks SHALL borrow `NodeProvider::get()`; the default `SharedNodeProvider` SHALL return one process-wide node. A null node SHALL fail initialization with `SystemError`.

#### Scenario: Logs become LogFrames

- **WHEN** an application initialized with `Iceoryx2Policy<'W','A','I','T'>` writes a log record
- **THEN** the record is published as a `LogFrame` on `Nexigen/Log/nexigen` with `app_id` `WAIT`

#### Scenario: Metrics and spans become MetricFrame and SpanFrame

- **WHEN** the application creates a counter and starts a span
- **THEN** the metric batch and span are published on `nexigen/metrics/stream` and `nexigen/trace/stream` with `app_id` `WAIT`

#### Scenario: Shared node failure is a status

- **WHEN** `NodeProvider::get()` returns `nullptr`
- **THEN** initialization fails with `SystemError` and no metric or trace sink is started

#### Scenario: Iceoryx2 policy is COM-gated

- **WHEN** the build has `NEXIGEN_ENABLE_COM=OFF`
- **THEN** `Iceoryx2Policy` is not available, while `ConsolePolicy`, `FilePolicy` and `NullPolicy` remain available
