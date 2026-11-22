# app/runtime Specification

## Purpose

Defines `App<Policy>`, the application facade that owns the settings bootstrap, time-base mode,
`Infrastructure` lifecycle, metric handles, one event input, an optional event output, and shutdown,
so a minimal application stays within a small `main` body.

## Requirements

### Requirement: App owns the application runtime lifecycle

`App<Policy>::create(argc, argv, app_name, mode)` SHALL return a heap-owned `App` that parses the
standard application options, creates the transport node, and initializes `Infrastructure`.
`run()` SHALL drive the application loop until a stop is requested. `shutdown()` SHALL be explicit
and idempotent; the destructor SHALL call it as a safety net. A second live `App` in the same
process SHALL fail with `Conflict`.

#### Scenario: Shutdown drains telemetry

- **WHEN** `shutdown()` is called after `run()`
- **THEN** the tracing, metrics, and logger components are quiesced in the existing order and the call is safe to repeat

#### Scenario: Second App is rejected

- **WHEN** a second `App` calls `create` while the first is alive
- **THEN** the second call returns `Conflict` and installs no watcher or workers

### Requirement: App bootstrap loads files or uses defaults

`App::create` SHALL load the four settings files when a settings directory is supplied, and SHALL
build default settings when neither `--global-settings` nor `--local-settings` is present. `--help`
SHALL print usage and return success with a null `App` pointer, which the caller turns into exit code 0.

#### Scenario: Create without a settings directory

- **WHEN** `App::create` receives only `--app-id` and no settings directory
- **THEN** it builds default settings, applies the CLI identity and log level, and initializes successfully

#### Scenario: Create with a settings directory

- **WHEN** `App::create` receives a valid settings directory
- **THEN** it loads the four subsystem files through the standard settings loader before initialization

#### Scenario: Help is not an error

- **WHEN** `--help` is supplied
- **THEN** `create` prints the usage text, returns success, and yields a null `App` pointer

### Requirement: Application identity is required

`App::create` SHALL require a non-empty canonical `app_id` from `--app-id` or the suite manifest. It
SHALL return `InvalidArgument` and name the missing option when the identifier is absent; it SHALL
NOT fall back to the application name or a compiled tag.

#### Scenario: Missing identifier fails create

- **WHEN** `App::create` runs without `--app-id` and without a manifest-provided identity
- **THEN** it returns `InvalidArgument`, prints a diagnostic naming `--app-id`, and never calls `initialize`

#### Scenario: Manifest identity is accepted

- **WHEN** the ExecutionManager passes a valid `--app-id` from the suite manifest
- **THEN** all `App` telemetry and event headers carry that identifier

### Requirement: Time base is external by default and embeddable explicitly

The `App::create` time-base mode SHALL default to `External`, which attaches to the published base and
fails `create` when no usable base exists. `Embedded` SHALL create a local published base for
examples and tests before initializing `Infrastructure`; it SHALL be selected explicitly and never
as an implicit fallback.

#### Scenario: Missing external base fails

- **WHEN** `External` mode runs without a usable published base
- **THEN** `create` fails before any application worker starts

#### Scenario: Embedded mode runs standalone

- **WHEN** `Embedded` mode runs without `nexigen-chronod`
- **THEN** the local base is published, `Infrastructure` attaches to it, and the application reaches its run loop

### Requirement: Declared instruments and ports must be valid before run

`App` SHALL remember every failed counter, gauge, histogram, subscription, or publication request as
a `core::SettingsDiagnostic`. `run()` SHALL return `InvalidArgument` without entering the loop when
any recorded diagnostic is an error, and the diagnostic SHALL name each failing declaration.

#### Scenario: Failed counter blocks run

- **WHEN** `counter("signals_processed")` fails because the instrument registry is full
- **THEN** `run()` returns `InvalidArgument`, names `signals_processed`, and the loop never starts

#### Scenario: Failed subscription blocks run

- **WHEN** `subscribe<Signal>` fails because the service or listener cannot be created
- **THEN** `run()` returns `InvalidArgument`, names the service, and no callback is invoked

### Requirement: One event input and an optional event output

`App` SHALL expose `subscribe<T>(service, callback)` for one input type and
`publish<T>(service)` for an optional output type. The subscription and the borrowed
`com::EventProducer<T>` handle SHALL stamp the application's `app_id` into the event header, and
`run()` SHALL wait on the input through a bounded `EventConsumerSet`/WaitSet timeout so an idle
application still observes stop requests.

#### Scenario: Input is delivered to the callback

- **WHEN** an event arrives on the subscribed service
- **THEN** the registered callback runs on the `run()` thread with the payload and event header

#### Scenario: Output carries the application identity

- **WHEN** the callback uses the publisher handle
- **THEN** the published event header carries the `App` `app_id` and the configured event code

#### Scenario: Idle input still observes stop

- **WHEN** no event arrives for longer than the wait timeout
- **THEN** `run()` re-checks the stop request and time-base state instead of blocking forever

### Requirement: Graceful signals and exit codes

`App` SHALL treat `SIGINT`, `SIGTERM`, and `SIGHUP` as graceful stops through the existing
`Infrastructure` watcher, and `run()` SHALL return the corresponding stop reason. `SIGPIPE` SHALL be
ignored for the `App` lifetime and restored after telemetry quiescence.

#### Scenario: SIGHUP drains and exits cleanly

- **WHEN** `SIGHUP` is delivered to a running `App`
- **THEN** `run()` returns `Sighup`, `shutdown()` drains telemetry, and the process exits with code 0

#### Scenario: SIGPIPE cannot kill the process

- **WHEN** a console write targets a closed pipe during the `App` lifetime
- **THEN** the write reports `EPIPE` and the process continues to its normal stop path

#### Scenario: Exit code maps the stop reason

- **WHEN** the stop reason is `Requested`, `Sigint`, `Sigterm`, or `Sighup`
- **THEN** the application exit code is 0; when it is `TimeBaseLost`, the code is 75

### Requirement: Runtime vocabulary is serializable

`TimeBaseMode` SHALL provide `External` and `Embedded` with `to_string`/`from_string` (`external`,
`embedded`), non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a
`std::formatter` specialization.

#### Scenario: Mode names round-trip

- **WHEN** each `TimeBaseMode` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** the text and JSON are the lowercase name and the parsed value equals the original enumerator

#### Scenario: Mode formats directly

- **WHEN** `std::format("{}", TimeBaseMode::Embedded)` is evaluated
- **THEN** the result is `embedded`

### Requirement: App exposes cold-path diagnostics

`App<Policy>` SHALL provide a `to_string()` member and a `std::formatter` specialization reporting
the application name, time-base mode, initialized state, and diagnostic count. `diagnostics()` SHALL
return the collected `core::SettingsDiagnostic` values without allocating on the caller's side.

#### Scenario: App renders its state

- **WHEN** `to_string()` and `std::format("{}", app)` are evaluated
- **THEN** both name the application, time-base mode, initialized state, and diagnostic count, and the formatter matches `to_string()`

#### Scenario: Diagnostics are inspectable

- **WHEN** a counter or port declaration fails
- **THEN** `diagnostics()` contains a `core::SettingsDiagnostic` naming that declaration
