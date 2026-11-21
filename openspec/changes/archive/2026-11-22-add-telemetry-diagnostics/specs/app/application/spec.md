# Spec Delta

## MODIFIED Requirements

### Requirement: Initialization follows a fixed order

`Infrastructure::initialize(const Settings&)` SHALL claim the process-global signal ownership, block `SIGINT`/`SIGTERM`, construct a synchronous bootstrap diagnostics writer, attach to the time-base daemon (reporting an attach failure through that writer) and fail initialization when no usable base is published, create the logger, bind the time-base client to the logger diagnostics adapter, construct the metric provider and check its required worker-start result, construct and initialize the tracing provider, bind the metric and tracer providers to the same adapter, and start the signal watcher. A failure at any step, including a `Required` thread-attribute failure or an unusable time base, SHALL release ownership, tear down partial state in reverse dependency order, restore the signal mask, and return a `core::Status`. Failures of the metric or tracing provider after the logger is ready SHALL be logged once before rollback. The signal watcher SHALL retain its existing configuration; no separate app settings document is introduced.

#### Scenario: Logger is ready before providers start

- **WHEN** initialization begins with valid logging, metrics, tracing, and chrono settings and a running daemon
- **THEN** the time base is attached first, the logger is configured before the metric or tracing worker threads start, and every later diagnostic can be logged

#### Scenario: Missing time base fails initialization

- **WHEN** initialization runs while the time-base daemon is absent or not usable
- **THEN** it returns a failure status before creating the logger, metric provider, or tracer provider

#### Scenario: Attach failure reaches the bootstrap channel

- **WHEN** initialization cannot attach to the time base
- **THEN** the bootstrap writer receives one `Degraded` event carrying the failure code before `initialize` returns

#### Scenario: Diagnostics are bound before providers start

- **WHEN** initialization succeeds
- **THEN** the time-base client, metric provider, and tracer provider are bound to the logger adapter before their workers are started or immediately after construction

#### Scenario: Provider startup failures are logged

- **WHEN** the metric or tracing provider fails its required startup check after the logger is ready
- **THEN** one error record naming the failing subsystem is logged before rollback

#### Scenario: Failure rolls back partial initialization

- **WHEN** the tracer provider fails to initialize after the logger and metric provider were constructed
- **THEN** `initialize` releases signal ownership, destroys the started components, restores the signal mask, and returns the failure status

#### Scenario: Required worker-attribute failure starts no non-RT fallback

- **WHEN** any logger/provider worker cannot apply a required FIFO/RR or affinity attribute after earlier components started
- **THEN** that worker does not enter its main loop, earlier components are stopped and destroyed in the existing rollback order, and initialization returns `SystemError`

#### Scenario: Best-effort partial application does not trigger rollback

- **WHEN** a worker explicitly uses `BestEffort` and one attribute is rejected
- **THEN** initialization continues with the partial state exposed in that worker's startup report

#### Scenario: Repeated initialization is idempotent

- **WHEN** `initialize` is called again on the same initialized `Infrastructure`
- **THEN** it returns OK without recreating components or the signal watcher
