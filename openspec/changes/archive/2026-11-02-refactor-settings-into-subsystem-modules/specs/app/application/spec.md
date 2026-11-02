# Spec Delta

## MODIFIED Requirements

### Requirement: Initialization follows a fixed order

`Infrastructure::initialize(const Settings&)` SHALL claim the process-global signal ownership, block `SIGINT`/`SIGTERM`, construct the chrono provider (without starting its worker), create the logger, bind the logger to the chrono provider and start its background worker, construct the metric provider, construct and initialize the tracing provider, and start the signal watcher. A failure at any step SHALL release ownership, tear down partial state, restore the signal mask, and return a `core::Status`.

#### Scenario: Logger is ready before providers start

- **WHEN** initialization begins with valid logging, metrics, tracing, and chrono settings
- **THEN** the chrono provider is constructed first, the logger is configured before the chrono, metric, or tracing worker threads start, and every later diagnostic can be logged

#### Scenario: Failure rolls back partial initialization

- **WHEN** the tracer provider fails to initialize after the metric and chrono providers were constructed
- **THEN** `initialize` releases signal ownership, destroys the started components, restores the signal mask, and returns the failure status

#### Scenario: Repeated initialization is idempotent

- **WHEN** `initialize` is called again on the same initialized `Infrastructure`
- **THEN** it returns OK without recreating components or the signal watcher

### Requirement: Runnable minimal infrastructure example

The project SHALL provide an `ex_app_lifecycle` executable that uses `Infrastructure` directly in `main` with a stateless policy and **no application class**: create it through `make_infrastructure`, initialize it with default `Settings`, log, create and record a metric, open and finish a span, take a chrono snapshot, run a few ticks, request a stop, shut down, and exit 0.

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

### Requirement: Command-line options initialize the infrastructure

`Infrastructure::initialize(const Command_line_options& options, Settings& loaded)` SHALL map the parsed directories to `AppSettingsPaths`, call `load_app_settings`, apply the CLI log level to `loaded`, and, only when loading succeeds, call the existing `initialize(const Settings&)` overload. The existing overload SHALL remain unchanged.

#### Scenario: Valid options initialize from loaded settings

- **WHEN** parsed options name a readable global or local settings directory and initialization is called with them
- **THEN** `loaded` contains the four merged subsystem settings, the CLI level is applied, and the infrastructure initializes through the existing path

#### Scenario: Existing settings overload still works

- **WHEN** application code calls `initialize(const Settings&)` directly
- **THEN** it behaves exactly as before, without parsing or loading files
