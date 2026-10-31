# app/application Specification

## Purpose

Defines the reusable application lifecycle: ownership and initialization order of the logging, metrics, and tracing infrastructure, graceful SIGINT/SIGTERM handling with a logged stop reason, and an idempotent shutdown path for real-time applications.

## Requirements

### Requirement: Initialization follows a fixed order

`Infrastructure::initialize(const App_settings&)` SHALL claim the process-global signal ownership, block `SIGINT`/`SIGTERM`, create the policy components, construct the metric provider, construct and initialize the tracing provider, and start the signal watcher. A failure at any step SHALL release ownership, tear down partial state, restore the signal mask, and return a `core::Status`.

#### Scenario: Logger is ready before providers start

- **WHEN** initialization begins with valid logging, metrics, and tracing settings
- **THEN** the logger is configured before any metric harvester or tracer consumer thread starts, and every later diagnostic can be logged

#### Scenario: Failure rolls back partial initialization

- **WHEN** the tracer provider fails to initialize after the metric provider was constructed
- **THEN** `initialize` releases signal ownership, destroys the started components, restores the signal mask, and returns the failure status

#### Scenario: Repeated initialization is idempotent

- **WHEN** `initialize` is called again on the same initialized `Infrastructure`
- **THEN** it returns OK without recreating components or the signal watcher

### Requirement: Run loop observes the stop request

`Infrastructure::run(Tick&&)` SHALL execute the caller's tick callback while no stop is requested and return the `Infrastructure_stop_reason`. A bool-returning tick SHALL request a host stop when it returns false; a void tick SHALL run until a signal or an explicit `request_stop` call. The loop SHALL perform no allocation and no lock.

#### Scenario: Signal stops the loop

- **WHEN** a SIGINT or SIGTERM is delivered while `run` is executing ticks
- **THEN** the loop finishes the current tick, exits without calling the tick again, and returns `Sigint` or `Sigterm` respectively

#### Scenario: Host can request a stop

- **WHEN** application code calls `request_stop(Infrastructure_stop_reason::Requested)` or a bool tick returns false
- **THEN** `run` exits at the next check and returns `Requested`

#### Scenario: First stop reason wins

- **WHEN** two stop causes occur before the loop exits
- **THEN** the reason recorded first is returned unchanged by later requests

### Requirement: Shutdown is idempotent and flushes in order

`Infrastructure::shutdown()` SHALL stop and join the signal watcher, log exactly one final record carrying the stop reason, flush the tracer provider, flush metrics, flush the logger, restore the caller's previous signal mask, and release process-global signal ownership. The destructor SHALL call `shutdown()` when initialization succeeded; repeated calls SHALL be safe and do nothing after the first.

#### Scenario: Every accepted record is flushed

- **WHEN** an infrastructure with queued log records, metrics, and spans shuts down
- **THEN** tracer, metrics, and logger flushes complete before `shutdown` returns, and their failures are surfaced

#### Scenario: Shutdown reason is logged once

- **WHEN** shutdown runs after any stop cause
- **THEN** the application logger contains exactly one final record naming the reason (`requested`, `sigint`, or `sigterm`)

#### Scenario: Double shutdown is harmless

- **WHEN** `shutdown()` is called explicitly and then again by the destructor
- **THEN** the second call performs no work and returns normally

#### Scenario: Shutdown releases signal ownership

- **WHEN** `shutdown()` completes
- **THEN** another `Infrastructure` can initialize and claim the signal ownership

### Requirement: SIGINT and SIGTERM request a graceful stop

The system SHALL block `SIGINT` and `SIGTERM` before any logging, metrics, or tracing worker thread is created, watch them through a `signalfd` on a dedicated watcher thread, and translate them into `Infrastructure_stop_reason::Sigint`/`Sigterm`. The watcher SHALL NOT run application code beyond recording the reason and requesting the stop.

#### Scenario: Signals cannot terminate the process abruptly

- **WHEN** SIGINT or SIGTERM is delivered after initialization
- **THEN** the default process termination is suppressed, the watcher records the reason, and the application exits through `run` → `shutdown`

#### Scenario: Workers inherit the blocked mask

- **WHEN** logging, metric, or tracing workers are created after signal masking
- **THEN** they inherit the blocked `SIGINT`/`SIGTERM` mask and cannot consume the signal themselves

### Requirement: Runnable application lifecycle example

The project SHALL provide an `ex_app_lifecycle` executable that loads temporary logging/metrics/tracing settings pairs, creates an `Infrastructure` through `make_infrastructure`, initializes it, demonstrates logger records, a metric created through `meter()` and recorded, and a tracer obtained through `tracer_for()` with a span, runs ticks, raises SIGINT from a helper thread, shuts down, verifies the shutdown reason was logged, and exits 0.

#### Scenario: Example exercises logging, metrics and tracing

- **WHEN** `ex_app_lifecycle` runs without arguments
- **THEN** it creates its settings files, initializes the infrastructure, logs through `logger()`, creates and increments a metric through `meter()`, starts and finishes a span through `tracer_for()`, and verifies the produced log/metric/trace output

#### Scenario: Example exercises the signal path

- **WHEN** the example raises SIGINT after a few ticks
- **THEN** `run` returns `Sigint`, `shutdown` writes the reason record, flushes all three subsystems, releases signal ownership, and the process exits 0

#### Scenario: Example reports failures

- **WHEN** settings loading, infrastructure creation, initialization, or verified output fails
- **THEN** the example prints the failure and exits with a non-zero status

### Requirement: Heap-only infrastructure factory

The system SHALL provide `make_infrastructure<Policy, Tracer_capacity, Tracer_threads>()` returning `core::Result<std::unique_ptr<Infrastructure<...>>>`. `Infrastructure` construction SHALL be private to the factory, so automatic and static `Infrastructure` objects cannot be created; allocation failure SHALL return `SystemError` without terminating.

#### Scenario: Stack construction is impossible

- **WHEN** application code attempts to declare an `Infrastructure` object directly
- **THEN** the code does not compile because the constructor is private

#### Scenario: Factory returns heap ownership

- **WHEN** `make_infrastructure` succeeds
- **THEN** it returns a non-null `unique_ptr` whose `Infrastructure` is fully constructed and can be initialized

#### Scenario: Allocation failure is a status

- **WHEN** the heap allocation fails
- **THEN** the factory returns `ErrorCode::SystemError` without calling `std::terminate`

### Requirement: Stateless infrastructure policy

The system SHALL require a `Policy` with nested `Logger`, `Metrics_sink`, and `Trace_sink` types and static `emplace_logger`, `emplace_metrics_sink`, and `emplace_trace_sink` methods returning `core::Status`. `Infrastructure` SHALL enforce the logger/sink concepts at compile time; the policy SHALL hold no runtime state.

#### Scenario: Policy types are validated at compile time

- **WHEN** a policy supplies component types and emplacement methods
- **THEN** `Infrastructure` instantiates only if the logger and both sinks satisfy their concepts

#### Scenario: Components outlive the providers

- **WHEN** an initialized infrastructure is destroyed or shut down
- **THEN** providers are destroyed before the sinks and logger they reference

### Requirement: Process-global signal ownership guard

`Infrastructure::initialize` SHALL claim a non-template process-global signal ownership before blocking signals or starting workers. While another `Infrastructure` (of any policy) owns it, `initialize` SHALL return `ErrorCode::Conflict`. Rollback and `shutdown` SHALL release ownership.

#### Scenario: Second infrastructure is rejected

- **WHEN** a second `Infrastructure` calls `initialize` while the first is initialized
- **THEN** the second call returns `Conflict` and installs no watcher

#### Scenario: Different policies still conflict

- **WHEN** two `Infrastructure` specializations with different policies are initialized concurrently
- **THEN** only one succeeds; the other returns `Conflict`

#### Scenario: Failed initialization releases ownership

- **WHEN** a policy hook fails after ownership was claimed
- **THEN** ownership is released and a later `initialize` can succeed

### Requirement: Infrastructure exposes logger, metric and tracer factories

`Infrastructure` SHALL expose `logger()` for logging, `metrics()` plus `meter(name, version)` returning `core::Result<metrics::Meter*>`, and `tracer()` plus `tracer_for(scope)` returning `core::Result<Tracer_type*>`. Factory conveniences SHALL return `NotInitialized` before initialization; handles SHALL remain valid while the infrastructure is alive.

#### Scenario: Logger access is the application logging path

- **WHEN** application code calls `infra.logger().info("tick {}", n)`
- **THEN** the record goes through the configured logger and is flushed on shutdown

#### Scenario: Metric creation goes through the infrastructure

- **WHEN** application code calls `infra.meter("sim", "1.0")` and creates a counter
- **THEN** the counter belongs to the infrastructure's `MetricProvider` and its value is visible in the provider snapshot

#### Scenario: Tracer creation goes through the infrastructure

- **WHEN** application code calls `infra.tracer_for("sim.control")` and starts a span
- **THEN** the span is accepted and flushed on shutdown

#### Scenario: Factory access before initialization is rejected

- **WHEN** `meter()` or `tracer_for()` is called before successful initialization
- **THEN** it returns `ErrorCode::NotInitialized` without dereferencing an empty provider

### Requirement: Infrastructure stop reason is serializable

`Infrastructure_stop_reason` SHALL provide `None`, `Requested`, `Sigint`, and `Sigterm` with `to_string`/`from_string` (`none`, `requested`, `sigint`, `sigterm`), non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization.

#### Scenario: Reason round-trips through JSON

- **WHEN** each `Infrastructure_stop_reason` value passes through `to_json` then `from_json`
- **THEN** the JSON is the lowercase name and the recovered value equals the original enumerator

#### Scenario: Reason formats directly

- **WHEN** `std::format("{}", Infrastructure_stop_reason::Sigint)` is evaluated
- **THEN** the result is `"sigint"`
