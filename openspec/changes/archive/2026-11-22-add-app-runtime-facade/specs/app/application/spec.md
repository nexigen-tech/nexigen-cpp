# Spec Delta

## MODIFIED Requirements

### Requirement: Run loop observes the stop request

`Infrastructure::run(Tick&&)` SHALL execute the caller's tick callback while no stop is requested and return the `Infrastructure_stop_reason`. A bool-returning tick SHALL request a host stop when it returns false; a void tick SHALL run until a signal or an explicit `request_stop` call. The loop SHALL perform no allocation and no lock.

#### Scenario: Signal stops the loop

- **WHEN** a SIGINT, SIGTERM, or SIGHUP is delivered while `run` is executing ticks
- **THEN** the loop finishes the current tick, exits without calling the tick again, and returns `Sigint`, `Sigterm`, or `Sighup` respectively

#### Scenario: Host can request a stop

- **WHEN** application code calls `request_stop(Infrastructure_stop_reason::Requested)` or a bool tick returns false
- **THEN** `run` exits at the next check and returns `Requested`

#### Scenario: First stop reason wins

- **WHEN** two stop causes occur before the loop exits
- **THEN** the reason recorded first is returned unchanged by later requests

### Requirement: Shutdown is idempotent and flushes in order

`Infrastructure::shutdown()` SHALL stop and join the signal watcher, log exactly one final record carrying the stop reason, quiesce the tracer provider, quiesce the metric provider, flush the logger, and quiesce the logger, then restore the caller's previous signal mask and release process-global signal ownership. It SHALL NOT destroy component storage or sinks; those SHALL be released only by the destructor. Failures from tracer or metric quiescence SHALL be logged while the logger is still active, and the first failure SHALL be returned. The destructor SHALL call `shutdown()` when initialization succeeded; repeated calls SHALL be safe and do nothing after the first.

#### Scenario: Every accepted record is flushed

- **WHEN** an infrastructure with queued log records, metrics, spans, and chrono snapshots shuts down
- **THEN** tracer, metrics, logger, and chrono flushes complete before `shutdown` returns, and their failures are surfaced

#### Scenario: Shutdown reason is logged once

- **WHEN** shutdown runs after any stop cause
- **THEN** the application logger contains exactly one final record naming the reason (`requested`, `sigint`, `sigterm`, `sighup`, or `time_base_lost`)

#### Scenario: Double shutdown is harmless

- **WHEN** `shutdown()` is called explicitly and then again by the destructor
- **THEN** the second call performs no work and returns normally

#### Scenario: Shutdown releases signal ownership

- **WHEN** `shutdown()` completes
- **THEN** another `Infrastructure` can initialize and claim the signal ownership

#### Scenario: Telemetry storage survives shutdown

- **WHEN** `shutdown()` returns
- **THEN** the metric registry, tracer pool, logger rings, and their sinks are still alive, no sink receives another batch, and no memory is released

#### Scenario: Quiescence failures reach the log

- **WHEN** tracer or metric quiescence reports an error
- **THEN** a record describing that failure passes through the still-active logger before it is quiesced, and `shutdown()` returns the first failure

#### Scenario: Logger is quiesced last

- **WHEN** shutdown quiesces the telemetry components
- **THEN** tracing and metrics are quiesced before the logger, so their shutdown diagnostics are delivered, and the logger is the last telemetry component destroyed

### Requirement: Infrastructure stop reason is serializable

`InfrastructureStopReason` SHALL provide `None`, `Requested`, `Sigint`, `Sigterm`, `Sighup`, and
`TimeBaseLost` with `to_string`/`from_string` (`none`, `requested`, `sigint`, `sigterm`, `sighup`,
`time_base_lost`), non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a
`std::formatter` specialization.

#### Scenario: Reason round-trips through JSON

- **WHEN** each `InfrastructureStopReason` value passes through `to_json` then `from_json`
- **THEN** the JSON is the lowercase name and the recovered value equals the original enumerator

#### Scenario: Reason formats directly

- **WHEN** `std::format("{}", InfrastructureStopReason::Sigint)` is evaluated
- **THEN** the result is `"sigint"`

#### Scenario: SIGHUP reason formats and parses

- **WHEN** `InfrastructureStopReason::Sighup` is rendered with `to_string` and `std::format` and parsed back with `from_string` and JSON
- **THEN** every representation is `sighup` and the parsed value equals the original enumerator

#### Scenario: Lost time base is a distinct reason

- **WHEN** the client stops after holdover expiry
- **THEN** the recorded reason is `time_base_lost` and the final log record names it

## ADDED Requirements

### Requirement: SIGHUP requests a graceful stop

`Infrastructure` SHALL block `SIGHUP` together with `SIGINT` and `SIGTERM` before any telemetry
worker starts, watch it through the same `signalfd`, and translate it into
`Infrastructure_stop_reason::Sighup`. The shutdown record SHALL name `sighup`.

#### Scenario: SIGHUP cannot terminate the process abruptly

- **WHEN** SIGHUP is delivered after initialization
- **THEN** the default process termination is suppressed, the watcher records `Sighup`, and the application exits through `run` → `shutdown`

#### Scenario: Workers inherit the SIGHUP mask

- **WHEN** logging, metric, or tracing workers are created after signal masking
- **THEN** they inherit the blocked `SIGHUP` mask and cannot consume the signal themselves
