# examples/app-quickstart Specification

## Purpose

Defines the runnable quickstart example that proves the `App<Policy>` facade keeps a real
application's `main` body within the platform's target of roughly 20–30 readable lines.

## Requirements

### Requirement: Quickstart application stays within the line budget

The project SHALL provide a runnable `ex_app_quickstart` under `examples/app/` whose `main` body,
excluding includes, blank lines, and comments, is at most 30 lines. It SHALL use `App<Policy>` with
`TimeBaseMode::Embedded`, require `--app-id`, subscribe to one input service, and either print the
received value or publish a transformed value on one output service.

#### Scenario: Main body is small

- **WHEN** the example source is measured between the opening and closing braces of `main`, excluding includes, blank lines, and comments
- **THEN** the body is at most 30 lines

#### Scenario: Standalone run does not need chronod

- **WHEN** the example starts with `--app-id` and without `nexigen-chronod` or the ExecutionManager
- **THEN** the embedded time base is published, the infrastructure initializes, and the example reaches its run loop

#### Scenario: Missing app id is rejected

- **WHEN** the example starts without `--app-id`
- **THEN** it prints the `--app-id` diagnostic and exits non-zero before creating the infrastructure

### Requirement: Quickstart activity is recordable through iceoryx2

The example SHALL publish its logs, metrics, and spans through `Iceoryx2Policy`, and its input and
output events SHALL carry the application `app_id`. When `nexigen-recd` subscribes to the configured
services, the session SHALL contain the example's telemetry and event records under that identity.

#### Scenario: Recorder sees both telemetry and events

- **WHEN** `nexigen-recd` records the example's log, metric, trace, input, and output services
- **THEN** the session contains records attributed to the example `app_id`, including the event payloads

#### Scenario: Input drives the output

- **WHEN** a signal arrives on the input service
- **THEN** the example prints or publishes the transformed value and increments its processed counter
