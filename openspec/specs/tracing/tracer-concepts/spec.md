# tracing/tracer-concepts Specification

## Purpose

Defines the structural concepts `Tracer` and `TraceSink` that every tracing producer and sink satisfies, and the `NullTracer` / `NullTraceSink` implementations whose disabled path compiles away entirely.

## Requirements

### Requirement: Tracer structural concept

The system SHALL define `nexigen::tracing::concepts::Tracer` as a C++23 structural concept requiring `start_span(name, kind, optional parent context, source location)` returning an RAII guard that itself provides wait-free `set_attribute`, `set_status`, `end()`, `context()`, and a cold-path `to_string()`, plus a diagnostic `to_string()` on the tracer itself; the concept SHALL be satisfiable and checkable at compile time via `static_assert`.

#### Scenario: Conforming tracers satisfy the concept

- **WHEN** `static_assert(concepts::Tracer<T>)` is evaluated for the concrete `Tracer` and `NullTracer`
- **THEN** both satisfy the concept at compile time

#### Scenario: Non-conforming types are rejected

- **WHEN** a probe type missing `start_span`, or returning a guard without `end()`, is tested against the concept
- **THEN** the concept evaluates false at compile time

### Requirement: TraceSink structural concept

The system SHALL define `nexigen::tracing::concepts::TraceSink` as a structural concept requiring `start() noexcept -> core::Status`, `on_span(const SpanRecord&) noexcept -> core::Status`, `flush() noexcept -> core::Status`, `stop() noexcept`, and a cold-path `to_string()`, checkable at compile time via `static_assert`.

#### Scenario: Conforming sinks satisfy the concept

- **WHEN** `static_assert(concepts::TraceSink<S>)` is evaluated for `NullTraceSink`, `ConsoleTraceSink`, `JsonFileTraceSink`, `ChromeTraceSink`, and a `CompositeTraceSink` instantiation
- **THEN** every one satisfies the concept at compile time

#### Scenario: Non-conforming sinks are rejected

- **WHEN** a probe type missing `flush`, or with a throwing `on_span`, is tested against the concept
- **THEN** the concept evaluates false at compile time

### Requirement: NullTracer zero-instruction path

`NullTracer` SHALL satisfy `concepts::Tracer` with every operation an empty inline body: `start_span` returns a trivially destructible null guard whose attribute, status, end, and destruction operations are no-ops, so a compilation unit using only `NullTracer` performs no identifier generation, no timestamp reads, no thread-local work, and no memory writes when tracing is disabled — the disabled path compiles down to zero instructions in optimized builds.

#### Scenario: Null tracer satisfies the concept and produces nothing

- **WHEN** `static_assert(concepts::Tracer<NullTracer>)` is compiled and spans are started and destroyed through a `NullTracer` with the allocation counter armed and drains observed
- **THEN** the concept holds, zero records drain, zero heap allocations occur, and the null guard's lifetime is observable only as the return value

#### Scenario: Null tracer lifetime costs nothing

- **WHEN** a Release loop measures null span start plus destruction next to an empty baseline loop
- **THEN** the measured difference is within noise of the baseline — no identifier, timestamp, or stack work executes

### Requirement: NullTraceSink zero-work sink

`NullTraceSink` SHALL satisfy `concepts::TraceSink` with every operation an empty inline body: `start`/`flush` succeed as no-ops, `on_span` accepts and discards the record without reading it, and `stop` does nothing; it allocates nothing and writes nothing.

#### Scenario: Null sink accepts records without output

- **WHEN** records are delivered to a `NullTraceSink`
- **THEN** every delivery succeeds, no bytes are written to any descriptor, and no allocation occurs

#### Scenario: Null sink satisfies the concept

- **WHEN** `static_assert(concepts::TraceSink<NullTraceSink>)` is compiled
- **THEN** the concept holds

### Requirement: Null type serialization surfaces

`NullTracer` and `NullTraceSink` SHALL provide cold-path member and free `to_string()` and `std::formatter` specializations, and `to_json` without `from_json`, since null objects are not reconstructed from data.

#### Scenario: Null types render diagnostics

- **WHEN** `to_string()` and `std::format("{}", null_value)` are evaluated for both null types
- **THEN** both render a non-empty identifying string and the two agree

#### Scenario: Null types are not deserializable

- **WHEN** a translation unit attempts `from_json` into a null tracer or null sink
- **THEN** compilation fails because no overload exists
