# Spec Delta

## MODIFIED Requirements

### Requirement: Iceoryx2 policy publishes all telemetry as fixed frames

`Iceoryx2Policy<A,B,C,D,NodeProvider>` SHALL provide `Iceoryx2Logger`, `Iceoryx2MetricSink` and
`Iceoryx2TraceSink`, use `Nexigen/Log/nexigen`, `nexigen/metrics/stream` and
`nexigen/trace/stream`, and stamp the effective 16-byte application identifier into every frame.
The effective identifier SHALL come from the runtime application settings; the four template
characters SHALL be used only as a fallback when the runtime identifier is unset. The metric and
trace sinks SHALL borrow `NodeProvider::get()`; the default `SharedNodeProvider` SHALL return one
process-wide node. A null node SHALL fail initialization with `SystemError`.

#### Scenario: Logs become LogFrames

- **WHEN** an application initialized with `Iceoryx2Policy<'W','A','I','T'>` supplies the runtime
  identifier `CN_LOOP_1` and writes a log record
- **THEN** the record is published as a `LogFrame` on `Nexigen/Log/nexigen` with `app_id`
  `CN_LOOP_1`, not `WAIT`

#### Scenario: Metrics and spans become MetricFrame and SpanFrame

- **WHEN** the application creates a counter and starts a span with runtime identifier `CN_LOOP_1`
- **THEN** the metric batch and span are published on `nexigen/metrics/stream` and
  `nexigen/trace/stream` with `app_id` `CN_LOOP_1`

#### Scenario: Template tag is the standalone fallback

- **WHEN** an application initialized with `Iceoryx2Policy<'W','A','I','T'>` supplies no runtime
  identifier and writes a log record
- **THEN** the published `LogFrame` carries `WAIT`

#### Scenario: Full-width identifiers travel unchanged

- **WHEN** the runtime identifier is a valid 16-byte value
- **THEN** the log, metric, and trace frames carry all 16 bytes without truncation

#### Scenario: Shared node failure is a status

- **WHEN** `NodeProvider::get()` returns `nullptr`
- **THEN** initialization fails with `SystemError` and no metric or trace sink is started

#### Scenario: Iceoryx2 policy is COM-gated

- **WHEN** the build has `NEXIGEN_ENABLE_COM=OFF`
- **THEN** `Iceoryx2Policy` is not available, while `ConsolePolicy`, `FilePolicy` and `NullPolicy`
  remain available
