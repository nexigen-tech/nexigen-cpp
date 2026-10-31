# Spec Delta

## MODIFIED Requirements

### Requirement: Initialization follows a fixed order

`Infrastructure::initialize(const App_settings&)` SHALL claim the process-global signal ownership, block `SIGINT`/`SIGTERM`, construct the chrono provider (without starting its worker), create the logger, bind the logger to the chrono provider and start its background worker, construct the metric provider, construct and initialize the tracing provider, and start the signal watcher. A failure at any step SHALL release ownership, tear down partial state, restore the signal mask, and return a `core::Status`.

#### Scenario: Logger is ready before providers start

- **WHEN** initialization begins with valid logging, metrics, tracing, and chrono settings
- **THEN** the chrono provider is constructed first, the logger is configured before the chrono, metric, or tracing worker threads start, and every later diagnostic can be logged

#### Scenario: Failure rolls back partial initialization

- **WHEN** the tracer provider fails to initialize after the metric and chrono providers were constructed
- **THEN** `initialize` releases signal ownership, destroys the started components, restores the signal mask, and returns the failure status

#### Scenario: Repeated initialization is idempotent

- **WHEN** `initialize` is called again on the same initialized `Infrastructure`
- **THEN** it returns OK without recreating components or the signal watcher

### Requirement: Shutdown is idempotent and flushes in order

`Infrastructure::shutdown()` SHALL stop and join the signal watcher, log exactly one final record carrying the stop reason, flush the tracer provider, flush metrics, flush the logger, flush the chrono provider, restore the caller's previous signal mask, and release process-global signal ownership. The destructor SHALL call `shutdown()` when initialization succeeded; repeated calls SHALL be safe and do nothing after the first.

#### Scenario: Every accepted record is flushed

- **WHEN** an infrastructure with queued log records, metrics, spans, and chrono snapshots shuts down
- **THEN** tracer, metrics, logger, and chrono flushes complete before `shutdown` returns, and their failures are surfaced

#### Scenario: Shutdown reason is logged once

- **WHEN** shutdown runs after any stop cause
- **THEN** the application logger contains exactly one final record naming the reason (`requested`, `sigint`, or `sigterm`)

#### Scenario: Double shutdown is harmless

- **WHEN** `shutdown()` is called explicitly and then again by the destructor
- **THEN** the second call performs no work and returns normally

#### Scenario: Shutdown releases signal ownership

- **WHEN** `shutdown()` completes
- **THEN** another `Infrastructure` can initialize and claim the signal ownership

### Requirement: Stateless infrastructure policy

The system SHALL require a `Policy` with nested `Logger`, `Metrics_sink`, `Trace_sink`, and `Chrono_sink` types and static `emplace_logger`, `emplace_metrics_sink`, `emplace_trace_sink`, and `emplace_chrono_sink` methods returning `core::Status`. `Infrastructure` SHALL enforce the logger/sink concepts at compile time; the policy SHALL hold no runtime state.

#### Scenario: Policy types are validated at compile time

- **WHEN** a policy supplies component types and emplacement methods
- **THEN** `Infrastructure` instantiates only if the logger and every sink satisfy their concepts

#### Scenario: Components outlive the providers

- **WHEN** an initialized infrastructure is destroyed or shut down
- **THEN** providers are destroyed before the sinks and logger they reference

### Requirement: Infrastructure exposes logger, metric and tracer factories

`Infrastructure` SHALL expose `logger()` for logging, `metrics()` plus `meter(name, version)` returning `core::Result<metrics::Meter*>`, `tracer()` plus `tracer_for(scope)` returning `core::Result<Tracer_type*>`, and `chrono()` returning the `chrono::ChronoProvider`. Factory conveniences SHALL return `NotInitialized` before initialization; handles SHALL remain valid while the infrastructure is alive.

#### Scenario: Logger access is the application logging path

- **WHEN** application code calls `infra.logger().info("tick {}", n)`
- **THEN** the record goes through the configured logger and is flushed on shutdown

#### Scenario: Metric creation goes through the infrastructure

- **WHEN** application code calls `infra.meter("sim", "1.0")` and creates a counter
- **THEN** the counter belongs to the infrastructure's `MetricProvider` and its value is visible in the provider snapshot

#### Scenario: Tracer creation goes through the infrastructure

- **WHEN** application code calls `infra.tracer_for("sim.control")` and starts a span
- **THEN** the span is accepted and flushed on shutdown

#### Scenario: Chrono access exposes the clock service

- **WHEN** application code calls `infra.chrono().snapshot_now()`
- **THEN** a snapshot is delivered through the infrastructure's configured chrono sink and the provider is flushed on shutdown

#### Scenario: Factory access before initialization is rejected

- **WHEN** `meter()` or `tracer_for()` is called before successful initialization
- **THEN** it returns `ErrorCode::NotInitialized` without dereferencing an empty provider

### Requirement: Runnable minimal infrastructure example

The project SHALL provide an `ex_app_lifecycle` executable that uses `Infrastructure` directly in `main` with a stateless policy and **no application class**: create it through `make_infrastructure`, initialize it with default `App_settings`, log, create and record a metric, open and finish a span, take a chrono snapshot, run a few ticks, request a stop, shut down, and exit 0.

#### Scenario: Example exercises logging, metrics and tracing

- **WHEN** `ex_app_lifecycle` runs without arguments
- **THEN** it creates the infrastructure, logs through `logger()`, creates and increments a counter through `meter()`, starts and finishes a span through `tracer_for()`, takes a snapshot through `chrono()`, and exits 0

#### Scenario: Example uses a direct main flow

- **WHEN** the example source is read
- **THEN** it contains a `main` function and a stateless policy, with no application class and no settings-file parsing

#### Scenario: Example stops and flushes

- **WHEN** the example finishes a few ticks and requests a stop
- **THEN** `run` returns `Requested`, `shutdown` flushes all four subsystems and returns OK, and the process exits 0

#### Scenario: Example reports failures

- **WHEN** infrastructure creation, initialization, or shutdown fails
- **THEN** the example prints the failure and exits with a non-zero status
