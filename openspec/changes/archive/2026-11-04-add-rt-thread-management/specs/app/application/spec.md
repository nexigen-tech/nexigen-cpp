# Spec Delta

## MODIFIED Requirements

### Requirement: Initialization follows a fixed order

`Infrastructure::initialize(const Settings&)` SHALL claim the process-global signal ownership, block `SIGINT`/`SIGTERM`, construct the chrono provider (without starting its worker), create the logger, bind the logger to the chrono provider and start its background worker, construct the metric provider and check its required worker-start result, construct and initialize the tracing provider, and start the signal watcher. A failure at any step, including a `Required` thread-attribute failure, SHALL release ownership, tear down partial state in reverse dependency order, restore the signal mask, and return a `core::Status`. The signal watcher SHALL retain its existing configuration; no separate app settings document is introduced.

#### Scenario: Logger is ready before providers start

- **WHEN** initialization begins with valid logging, metrics, tracing, and chrono settings
- **THEN** the chrono provider is constructed first, the logger is configured before the chrono, metric, or tracing worker threads start, and every later diagnostic can be logged

#### Scenario: Failure rolls back partial initialization

- **WHEN** the tracer provider fails to initialize after the metric and chrono providers were constructed
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
