# Spec Delta

## MODIFIED Requirements

### Requirement: Shutdown is idempotent and flushes in order

`Infrastructure::shutdown()` SHALL stop and join the signal watcher, log exactly one final record carrying the stop reason, quiesce the tracer provider, quiesce the metric provider, flush the logger, and quiesce the logger, then restore the caller's previous signal mask and release process-global signal ownership. It SHALL NOT destroy component storage or sinks; those SHALL be released only by the destructor. Failures from tracer or metric quiescence SHALL be logged while the logger is still active, and the first failure SHALL be returned. The destructor SHALL call `shutdown()` when initialization succeeded; repeated calls SHALL be safe and do nothing after the first.

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

#### Scenario: Telemetry storage survives shutdown

- **WHEN** `shutdown()` returns
- **THEN** the metric registry, tracer pool, logger rings, and their sinks are still alive, no sink receives another batch, and no memory is released

#### Scenario: Quiescence failures reach the log

- **WHEN** tracer or metric quiescence reports an error
- **THEN** a record describing that failure passes through the still-active logger before it is quiesced, and `shutdown()` returns the first failure

#### Scenario: Logger is quiesced last

- **WHEN** shutdown quiesces the telemetry components
- **THEN** tracing and metrics are quiesced before the logger, so their shutdown diagnostics are delivered, and the logger is the last telemetry component destroyed

### Requirement: Infrastructure exposes logger, metric and tracer factories

`Infrastructure` SHALL expose `logger()` for logging, `metrics()` plus `meter(name, version)` returning `core::Result<metrics::Meter*>`, `tracer()` plus `tracer_for(scope)` returning `core::Result<Tracer_type*>`, and `chrono()` returning the attached time-base client handle. Every factory and control-plane convenience except the `chrono()` accessor SHALL return `NotInitialized` before initialization and once shutdown has begun. Handles already issued SHALL remain valid and memory-safe while the infrastructure is alive; the `chrono()` handle is the documented exception: it is valid only after a successful initialization and stops being usable at shutdown.

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

- **WHEN** application code calls `infra.chrono()` after initialization
- **THEN** it returns a handle reporting the current time-base status and the coefficient of each required domain, and the handle stops being usable at shutdown

#### Scenario: Factory access before initialization is rejected

- **WHEN** `meter()` or `tracer_for()` is called before successful initialization
- **THEN** it returns `ErrorCode::NotInitialized` without dereferencing an empty provider

#### Scenario: Chrono access requires successful initialization

- **WHEN** `chrono()` is called before successful initialization
- **THEN** the behavior is a documented lifetime violation (the accessor has no failure channel), and the handle is valid only after initialization and stops being usable at shutdown

#### Scenario: Control plane is closed after shutdown

- **WHEN** `meter()`, `tracer_for()`, `get_meter`, `get_tracer`, `collect_now`, or `flush` is called after `shutdown()` has begun
- **THEN** each returns `ErrorCode::NotInitialized` and no new meter, tracer, or instrument is created

#### Scenario: Issued handles survive shutdown

- **WHEN** a counter, gauge, statistics instrument, tracer, or logger obtained before shutdown is used after `shutdown()` returns
- **THEN** no memory error or sanitizer diagnostic occurs: metric records are accepted and discarded, tracer span starts report `NotInitialized`, and log records are dropped without blocking

#### Scenario: Handles do not survive the infrastructure

- **WHEN** an issued handle is used after the `Infrastructure` object has been destroyed
- **THEN** the behavior is a documented lifetime violation outside the supported lifecycle
