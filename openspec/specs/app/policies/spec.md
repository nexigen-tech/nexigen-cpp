# app/policies Specification

## Purpose
Provides ready-made stateless Infrastructure policies so applications can select console, JSONL-file, null, or iceoryx2 telemetry without re-implementing the same policy hooks.

## Requirements

### Requirement: Console policy maps all telemetry to the console

`ConsolePolicy` SHALL provide `ConsoleLogger`, `ConsoleMetricSink` and `ConsoleTraceSink` and initialize them from the supplied Infrastructure settings. The logger SHALL use the settings' level, worker spec and poll interval; the metric and trace sinks SHALL use their console defaults.

#### Scenario: Logs go through the console logger

- **WHEN** an Infrastructure initialized with `ConsolePolicy` writes a log record
- **THEN** the record is rendered by `ConsoleLogger`

#### Scenario: Metrics and spans use console sinks

- **WHEN** the application creates a counter and a span through the infrastructure
- **THEN** the metric batch and span are delivered to the console metric and trace sinks

### Requirement: File policy maps telemetry to configured JSONL files

`FilePolicy` SHALL provide `FileLogger`, `JsonFileMetricSink` and `JsonFileTraceSink`. It SHALL map `logging.options.log_root_directory` to the logger base directory, `metrics.sink` to the metric sink and `tracing.sink` to the trace sink, and SHALL inject the application name. It SHALL return `InvalidArgument` when a required path is empty or the logger capacity is exceeded.

#### Scenario: Valid paths produce three JSONL streams

- **WHEN** initialization receives a log root and non-empty metric and trace sink paths
- **THEN** log, metric and trace records are written to their configured files and carry the application name

#### Scenario: Missing metric or trace path is rejected

- **WHEN** `metrics.sink.path` or `tracing.sink.path` is empty
- **THEN** initialization fails with `InvalidArgument` before the application enters its run loop

#### Scenario: Logger capacity is checked

- **WHEN** configured ring, message or thread capacities exceed the `FileLogger<>` limits
- **THEN** initialization fails with `InvalidArgument`

### Requirement: Null policy discards all telemetry

`NullPolicy` SHALL provide `NullLogger`, `NullMetricSink` and `NullTraceSink` and SHALL initialize them without observable output.

#### Scenario: Every record is accepted and discarded

- **WHEN** an application logs, creates a metric and starts a span with `NullPolicy`
- **THEN** initialization and delivery succeed and no file, console or transport output is produced

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

### Requirement: Policies are stateless and satisfy the infrastructure policy contract

`ConsolePolicy`, `FilePolicy`, `NullPolicy` and `Iceoryx2Policy<...>` SHALL satisfy `InfrastructurePolicy` at compile time, hold no runtime state, and perform their work only during `Infrastructure::initialize`.

#### Scenario: Invalid policy is rejected at compile time

- **WHEN** a policy lacks a component type or an emplacement hook
- **THEN** `Infrastructure` does not instantiate

#### Scenario: Hooks run only on the cold path

- **WHEN** the application enters its tick loop
- **THEN** no policy hook runs and the policy introduces no allocation on the tick path

### Requirement: Policy diagnostics are total

Each policy SHALL provide a `to_string()` member, a free `to_string`, a `std::formatter` specialization and non-throwing `to_json`/`from_json` for cold-path diagnostics. `Iceoryx2Policy` SHALL include its application tag in the textual and JSON form.

#### Scenario: Console policy diagnostics

- **WHEN** `to_string()`, `std::format` and `to_json` are evaluated for `ConsolePolicy`
- **THEN** each returns a stable `ConsolePolicy` identity and `from_json` leaves the stateless object unchanged

#### Scenario: File policy diagnostics

- **WHEN** `to_string()`, `std::format` and `to_json` are evaluated for `FilePolicy`
- **THEN** each returns a stable `FilePolicy` identity and `from_json` leaves the stateless object unchanged

#### Scenario: Null policy diagnostics

- **WHEN** `to_string()`, `std::format` and `to_json` are evaluated for `NullPolicy`
- **THEN** each returns a stable `NullPolicy` identity and `from_json` leaves the stateless object unchanged

#### Scenario: Iceoryx2 policy diagnostics include the tag

- **WHEN** `to_string()`, `std::format` and `to_json` are evaluated for `Iceoryx2Policy<'W','A','I','T'>`
- **THEN** the text and JSON contain `WAIT` and `from_json` leaves the stateless object unchanged
