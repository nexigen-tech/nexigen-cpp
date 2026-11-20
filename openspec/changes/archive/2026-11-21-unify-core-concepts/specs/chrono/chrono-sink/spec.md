# Spec Delta

## MODIFIED Requirements

### Requirement: Chrono sink concept

`nexigen::chrono::concepts::ChronoSink` SHALL be a C++23 concept satisfied only by types satisfying `core::concepts::Sink<T, Chrono_snapshot>`: `on_event(const Chrono_snapshot&) noexcept -> core::Status` and `flush() noexcept -> core::Status`, with no virtual functions required.

#### Scenario: Conforming sinks satisfy the concept

- **WHEN** `concepts::ChronoSink<T>` is evaluated for `NullChronoSink`, `ConsoleChronoSink`, `JsonFileChronoSink`, and a `CompositeChronoSink` over them
- **THEN** it is `true` in a constant expression

#### Scenario: Missing or wrong member rejects the concept

- **WHEN** a type lacks `on_event`, lacks `flush`, or returns a non-`core::Status` type from either
- **THEN** `concepts::ChronoSink<T>` is `false`

#### Scenario: Sink callbacks are allocation-free

- **WHEN** any sink processes a snapshot under an armed allocation arena
- **THEN** no `operator new`, `delete`, `malloc`, `free`, or dynamic container growth occurs

#### Scenario: Old delivery name is rejected

- **WHEN** a type still exposes `on_snapshot`
- **THEN** `concepts::ChronoSink<T>` is `false`
