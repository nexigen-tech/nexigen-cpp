# Spec Delta

## MODIFIED Requirements

### Requirement: TraceSink structural concept

The system SHALL define `nexigen::tracing::concepts::TraceSink` as `core::concepts::LifecycleSink<S, SpanRecord>` plus a cold-path `to_string()`: `start() noexcept -> core::Status`, `on_event(const SpanRecord&) noexcept -> core::Status`, `flush() noexcept -> core::Status`, `stop() noexcept`, and `to_string()`, checkable at compile time via `static_assert`.

#### Scenario: Conforming sinks satisfy the concept

- **WHEN** `static_assert(concepts::TraceSink<S>)` is evaluated for `NullTraceSink`, `ConsoleTraceSink`, `JsonFileTraceSink`, `ChromeTraceSink`, and a `CompositeTraceSink` instantiation
- **THEN** every one satisfies the concept at compile time

#### Scenario: Non-conforming sinks are rejected

- **WHEN** a probe type missing `flush`, missing `stop`, or with a throwing `on_event`, is tested against the concept
- **THEN** the concept evaluates false at compile time

#### Scenario: Old delivery name is rejected

- **WHEN** a probe type still exposes `on_span`
- **THEN** `concepts::TraceSink` evaluates false
