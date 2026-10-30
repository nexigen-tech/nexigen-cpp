# app/application Specification

## Purpose

Defines the reusable application lifecycle: ownership and initialization order of the logging, metrics, and tracing infrastructure, graceful SIGINT/SIGTERM handling with a logged stop reason, and an idempotent shutdown path for real-time applications.

## Requirements

### Requirement: App owns infrastructure through a CRTP contract

The system SHALL provide `App<Derived, Logger, Metrics_sink, Trace_sink, Tracer_capacity, Tracer_threads>` owning the logger, both sinks, both providers, and the signal watcher. The derived application SHALL provide the three component types and emplacement hooks; the base SHALL enforce the logger/sink concepts at compile time.

#### Scenario: Derived components are validated at compile time

- **WHEN** a derived application declares `Logger`, `Metrics_sink`, and `Trace_sink` types and emplacement hooks
- **THEN** the base class instantiates only if the logger and both sinks satisfy their project concepts, and reports a clear static-assert otherwise

#### Scenario: Components outlive the providers

- **WHEN** an initialized application is destroyed or shut down
- **THEN** providers are destroyed before the sinks and logger they reference, so no provider ever touches a dead sink

### Requirement: Initialization follows a fixed order

`App::initialize(const App_settings&)` SHALL block `SIGINT`/`SIGTERM`, configure the derived logger from the logging settings, construct the metric provider, construct and initialize the tracing provider, install the signal watcher, and publish the `current()` pointer, in that order. A failure at any step SHALL leave no partially initialized infrastructure and return a `core::Status`.

#### Scenario: Logger is ready before providers start

- **WHEN** initialization begins with valid logging, metrics, and tracing settings
- **THEN** the logger is configured before any metric harvester or tracer consumer thread starts, and every later diagnostic can be logged

#### Scenario: Failure rolls back partial initialization

- **WHEN** the tracer provider fails to initialize after the metric provider was constructed
- **THEN** `initialize` tears down the already-started components, restores the signal mask, returns the failure status, and leaves `current()` null

#### Scenario: Repeated initialization is idempotent

- **WHEN** `initialize` is called on an already initialized application
- **THEN** it returns OK without recreating loggers, providers, or the signal watcher

### Requirement: Run loop observes the stop request

`App::run(Tick&&)` SHALL execute the caller's tick callback while no stop is requested and return the `App_stop_reason`. A tick callback returning `bool` SHALL request a host stop when it returns false; a callback returning void SHALL run until a signal or an explicit `request_stop` call. The loop SHALL perform no allocation and no lock.

#### Scenario: Signal stops the loop

- **WHEN** a SIGINT or SIGTERM is delivered while `run` is executing ticks
- **THEN** the loop finishes the current tick, exits without calling the tick again, and returns `Sigint` or `Sigterm` respectively

#### Scenario: Host can request a stop

- **WHEN** application code calls `request_stop(App_stop_reason::Requested)` or a bool tick returns false
- **THEN** `run` exits at the next check and returns `Requested`

#### Scenario: First stop reason wins

- **WHEN** two stop causes occur before the loop exits
- **THEN** the reason recorded first is returned unchanged by later requests

### Requirement: Shutdown is idempotent and flushes in order

`App::shutdown()` SHALL stop and join the signal watcher, log exactly one final record carrying the stop reason, flush the tracer provider, flush metrics, flush the logger, restore the caller's previous signal mask, and clear `current()`. The destructor SHALL call `shutdown()` when initialization succeeded; repeated calls SHALL be safe and do nothing after the first.

#### Scenario: Every accepted record is flushed

- **WHEN** an application with queued log records, metrics, and spans shuts down
- **THEN** tracer, metrics, and logger flushes complete before `shutdown` returns, and their failures are surfaced

#### Scenario: Shutdown reason is logged once

- **WHEN** shutdown runs after any stop cause
- **THEN** the application logger contains exactly one final record naming the reason (`requested`, `sigint`, or `sigterm`)

#### Scenario: Double shutdown is harmless

- **WHEN** `shutdown()` is called explicitly and then again by the destructor
- **THEN** the second call performs no work and returns normally

### Requirement: SIGINT and SIGTERM request a graceful stop

The system SHALL block `SIGINT` and `SIGTERM` before any logging, metrics, or tracing worker thread is created, watch them through a `signalfd` on a dedicated watcher thread, and translate them into `App_stop_reason::Sigint`/`Sigterm`. The watcher SHALL NOT run application code beyond recording the reason and requesting the stop.

#### Scenario: Signals cannot terminate the process abruptly

- **WHEN** SIGINT or SIGTERM is delivered after initialization
- **THEN** the default process termination is suppressed, the watcher records the reason, and the application exits through `run` → `shutdown`

#### Scenario: Workers inherit the blocked mask

- **WHEN** logging, metric, or tracing workers are created after signal masking
- **THEN** they inherit the blocked `SIGINT`/`SIGTERM` mask and cannot consume the signal themselves

### Requirement: Current accessor is non-owning

`App<Derived>::current()` SHALL return a pointer to the most recently initialized application instance, published after successful initialization and cleared during shutdown. It SHALL be a convenience accessor, not an owner, and SHALL NOT prevent tests from running multiple application instances sequentially.

#### Scenario: Current points at the live instance

- **WHEN** an application has initialized successfully
- **THEN** `App<Derived>::current()` returns that instance and its accessors yield the same logger/providers

#### Scenario: Shutdown clears the pointer

- **WHEN** `shutdown()` completes
- **THEN** `App<Derived>::current()` returns null

### Requirement: App stop reason is serializable

`App_stop_reason` SHALL provide `None`, `Requested`, `Sigint`, and `Sigterm` with `to_string`/`from_string` (`none`, `requested`, `sigint`, `sigterm`), non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization.

#### Scenario: Reason round-trips through JSON

- **WHEN** each `App_stop_reason` value passes through `to_json` then `from_json`
- **THEN** the JSON is the lowercase name and the recovered value equals the original enumerator

#### Scenario: Reason formats directly

- **WHEN** `std::format("{}", App_stop_reason::Sigint)` is evaluated
- **THEN** the result is `"sigint"`

### Requirement: App exposes logger, metric and tracer factories

`App` SHALL expose the initialized infrastructure: `logger()` for logging, `metrics()` plus a `meter(name, version)` convenience returning `core::Result<metrics::Meter*>`, and `tracer()` plus a `tracer_for(scope)` convenience returning `core::Result<Tracer_type*>`. The factory conveniences SHALL return `NotInitialized` before `initialize()` succeeds, and handles SHALL remain valid while the App is alive and initialized.

#### Scenario: Logger access is the application logging path

- **WHEN** application code calls `app.logger().info("tick {}", n)`
- **THEN** the record goes through the configured logger and is flushed on shutdown

#### Scenario: Metric creation goes through the App

- **WHEN** application code calls `app.meter("sim", "1.0")` and creates a counter with `create_counter<...>()`
- **THEN** the meter belongs to the App's `MetricProvider`, the counter records normally, and its value is visible in the provider snapshot

#### Scenario: Tracer creation goes through the App

- **WHEN** application code calls `app.tracer_for("sim.control")` and starts a span
- **THEN** the tracer belongs to the App's `TracerProvider`, the span is accepted, and it is flushed on shutdown

#### Scenario: Factory access before initialization is rejected

- **WHEN** `meter()` or `tracer_for()` is called before successful initialization
- **THEN** it returns `ErrorCode::NotInitialized` without dereferencing an empty provider

### Requirement: Runnable application lifecycle example

The project SHALL provide an `ex_app_lifecycle` executable that loads temporary logging/metrics/tracing settings pairs, initializes an `App`, demonstrates the three infrastructure paths (logger records, a metric created through `meter()` and recorded, a tracer obtained through `tracer_for()` with a span), runs ticks, raises SIGINT from a helper thread, shuts down, verifies the shutdown reason was logged, and exits 0.

#### Scenario: Example exercises logging, metrics and tracing

- **WHEN** `ex_app_lifecycle` runs without arguments
- **THEN** it creates its settings files, initializes the App, logs through `app.logger()`, creates and increments a metric through `app.meter()`, starts and finishes a span through `app.tracer_for()`, and verifies the produced log/metric/trace output

#### Scenario: Example exercises the signal path

- **WHEN** the example raises SIGINT after a few ticks
- **THEN** `run` returns `Sigint`, `shutdown` writes the reason record, flushes all three subsystems, and the process exits 0

#### Scenario: Example reports failures

- **WHEN** settings loading, initialization, or verified output fails
- **THEN** the example prints the failure and exits with a non-zero status
