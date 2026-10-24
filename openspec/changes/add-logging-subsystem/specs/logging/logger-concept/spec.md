# Spec Delta

## Purpose

Defines the structural API contract — the C++23 concept `nexigen::logging::concepts::Logger` — that every concrete logger in the subsystem must satisfy, so callers, tests, and `CompositeLogger` can depend on one uniform, virtual-free logger interface.

## ADDED Requirements

### Requirement: Logger concept core surface

The system SHALL provide C++23 concept `nexigen::logging::concepts::Logger` in namespace `nexigen::logging::concepts` that is satisfied only by types offering, without virtual dispatch: `set_level(LogLevel) noexcept`; `level() const noexcept` returning `LogLevel`; `log(LogLevel, std::source_location, std::string_view) noexcept` returning `nexigen::core::Status`; and `flush() noexcept` returning `nexigen::core::Status`.

#### Scenario: Conforming type satisfies the concept

- **WHEN** `concepts::Logger<T>` is evaluated for a type offering the full required surface
- **THEN** it is `true` in a constant expression

#### Scenario: Missing member rejects the concept

- **WHEN** `concepts::Logger<T>` is evaluated for a type lacking any one required member (for example `flush`)
- **THEN** it is `false`, and the rejection is detected at compile time

#### Scenario: Wrong signature rejects the concept

- **WHEN** `concepts::Logger<T>` is evaluated for a type whose `log` returns something other than `nexigen::core::Status` or whose `level` is not `const noexcept`
- **THEN** it is `false`

### Requirement: Logger concept convenience methods

The `Logger` concept SHALL additionally require the six convenience methods `verbose`, `debug`, `info`, `warning`, `error`, and `fatal`, each accepting a message view and defaulting its source-location argument to `std::source_location::current()`.

#### Scenario: Convenience set is part of the contract

- **WHEN** `concepts::Logger<T>` is evaluated for a type that provides the core surface but omits `fatal`
- **THEN** it is `false`

### Requirement: Universal conformance assertion

Every concrete logger type published by the subsystem (`NullLogger`, `ConsoleLogger`, `FileLogger`, `CompositeLogger<...>` over conforming members) SHALL be verified by a `static_assert(concepts::Logger<T>)` in the library or its test translation unit, so conformance is a compile-time guarantee, not a documentation promise.

#### Scenario: Published loggers are pinned

- **WHEN** the test translation unit is compiled
- **THEN** `static_assert`s pass for `NullLogger`, `ConsoleLogger`, `FileLogger`, and a `CompositeLogger` instantiation, and removing the corresponding members from a logger makes the build fail

### Requirement: Convenience methods carry call-site location

The six convenience methods SHALL tag each record with the declared severity level and a source location captured at the call site (via the defaulted `std::source_location::current()` argument), so a record's file, line, and function describe the `logger.info(...)` statement itself, not any wrapper inside the library.

#### Scenario: Location comes from the caller

- **WHEN** `logger.warning("disk low")` is called from file `app.cpp` line 42 inside function `drive()`
- **THEN** the resulting record's location fields read back as that file name, line 42, and that function name

#### Scenario: Explicit log path still available

- **WHEN** a caller invokes `log(level, location, message)` directly with its own location value
- **THEN** the record carries exactly that location and level
