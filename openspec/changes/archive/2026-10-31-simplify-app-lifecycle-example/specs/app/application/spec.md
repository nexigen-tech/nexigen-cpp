# Spec Delta

## REMOVED Requirements

### Requirement: Runnable application lifecycle example

**Reason**: The full settings-file/SIGINT integration demo is too heavy for a first-contact `Infrastructure` example and duplicates lifecycle coverage already provided by unit tests.

**Migration**: Use the new minimal example below; full settings/signal scenarios remain covered by `nexigen_app_tests`.

## ADDED Requirements

### Requirement: Runnable minimal infrastructure example

The project SHALL provide an `ex_app_lifecycle` executable that uses `Infrastructure` directly in `main` with a stateless policy and **no application class**: create it through `make_infrastructure`, initialize it with default `App_settings`, log, create and record a metric, open and finish a span, run a few ticks, request a stop, shut down, and exit 0.

#### Scenario: Example exercises logging, metrics and tracing

- **WHEN** `ex_app_lifecycle` runs without arguments
- **THEN** it creates the infrastructure, logs through `logger()`, creates and increments a counter through `meter()`, starts and finishes a span through `tracer_for()`, and exits 0

#### Scenario: Example uses a direct main flow

- **WHEN** the example source is read
- **THEN** it contains a `main` function and a stateless policy, with no application class and no settings-file parsing

#### Scenario: Example stops and flushes

- **WHEN** the example finishes a few ticks and requests a stop
- **THEN** `run` returns `Requested`, `shutdown` flushes all three subsystems and returns OK, and the process exits 0

#### Scenario: Example reports failures

- **WHEN** infrastructure creation, initialization, or shutdown fails
- **THEN** the example prints the failure and exits with a non-zero status
