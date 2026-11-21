# Spec Delta

## MODIFIED Requirements

### Requirement: Infrastructure exposes logger, metric and tracer factories

`Infrastructure` SHALL expose `logger()` returning `core::Result<Logger*>`, `metrics()` returning `core::Result<MetricProvider*>`, `meter(name, version)` returning `core::Result<metrics::Meter*>`, `tracer()` plus `tracer_for(scope)` returning `core::Result<Tracer_type*>`, and `chrono()` returning the attached time-base client handle. Every factory and control-plane convenience except the `chrono()` accessor SHALL return `NotInitialized` before initialization and once shutdown has begun. Handles already issued SHALL remain valid and memory-safe while the infrastructure is alive; the `chrono()` handle is the documented exception: it is valid only after a successful initialization and stops being usable at shutdown.

#### Scenario: Logger access is the application logging path

- **WHEN** application code calls `infra.logger()` after initialization and writes through the returned handle
- **THEN** the record goes through the configured logger and is flushed on shutdown

#### Scenario: Metrics access exposes the provider

- **WHEN** application code calls `infra.metrics()` after initialization
- **THEN** it returns the live `MetricProvider` and its control-plane calls operate on the infrastructure's provider

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

- **WHEN** `logger()`, `metrics()`, `meter()`, or `tracer_for()` is called before successful initialization
- **THEN** it returns `ErrorCode::NotInitialized` without dereferencing an empty provider

#### Scenario: Chrono access requires successful initialization

- **WHEN** `chrono()` is called before successful initialization
- **THEN** the behavior is a documented lifetime violation (the accessor has no failure channel), and the handle is valid only after initialization and stops being usable at shutdown

#### Scenario: Control plane is closed after shutdown

- **WHEN** `logger()`, `metrics()`, `meter()`, `tracer_for()`, `get_meter`, `get_tracer`, `collect_now`, or `flush` is called after `shutdown()` has begun
- **THEN** each returns `ErrorCode::NotInitialized` and no new handle, meter, tracer, or instrument is created

#### Scenario: Issued handles survive shutdown

- **WHEN** a logger handle, metric provider handle, counter, gauge, statistics instrument, tracer, or metrics provider obtained before shutdown is used after `shutdown()` returns
- **THEN** no memory error or sanitizer diagnostic occurs: metric records are accepted and discarded, tracer span starts report `NotInitialized`, and log records are dropped without blocking

#### Scenario: Handles do not survive the infrastructure

- **WHEN** an issued handle is used after the `Infrastructure` object has been destroyed
- **THEN** the behavior is a documented lifetime violation outside the supported lifecycle
