# Spec Delta

## Purpose

Holds the minimal generic concepts that more than one lower layer must name to stay independent:
a plain shared-memory message and the level-aware logger contract.

## ADDED Requirements

### Requirement: Message concept

`core::concepts::Message<T>` SHALL be satisfied exactly when `std::is_trivially_copyable_v<T>` is
true, so payloads are plain bytes with no pointers, virtual tables, or owning members.

#### Scenario: POD payload is accepted

- **WHEN** a plain struct of arithmetic members is checked
- **THEN** `core::concepts::Message<T>` is true

#### Scenario: Non-POD payload is rejected

- **WHEN** a type contains `std::string`, `std::vector`, a smart pointer, or a virtual function
- **THEN** the concept is false at compile time

### Requirement: Level logger concept

`core::concepts::LevelLogger<T>` SHALL require
`log(core::LogLevel, std::string_view) noexcept -> void`. Richer logger contracts, such as the
variadic logging logger, SHALL remain in their own module and MAY extend this concept.

#### Scenario: Minimal logger satisfies the concept

- **WHEN** a type exposes the level/message call shape
- **THEN** `core::concepts::LevelLogger<T>` is true

#### Scenario: Constrained module uses the shared concept

- **WHEN** a worker binds a logger
- **THEN** it constrains the type with `core::concepts::LevelLogger`, not a module-local copy

### Requirement: Logger concept

`core::concepts::Logger<T, Format>` SHALL require runtime level control
(`set_level`/`level`), the explicit-level variadic entry `log(LogLevel, Format<Args...>, Args&&...)`,
the six severity conveniences with the same wrapper, and `flush() noexcept -> core::Status`. The
wrapper template is supplied by the logger module, so core names no logging type. Every record
member SHALL be `noexcept` and return `void`.

#### Scenario: Logging loggers satisfy the concept

- **WHEN** `core::concepts::Logger<T, FormatWithLocation>` is checked against `NullLogger`,
  `ConsoleLogger`, `FileLogger`, and `CompositeLogger`
- **THEN** each satisfies it at compile time

#### Scenario: Missing convenience is rejected

- **WHEN** a probe lacks `warning()` or returns a value from `info()`
- **THEN** the concept is false

#### Scenario: Variadic packs are accepted

- **WHEN** a logger is called as `info("value {}", 1)`
- **THEN** the call shape is part of the concept and the logger satisfies it

#### Scenario: Synchro accepts the alias

- **WHEN** a `logging::NullLogger` is passed to the synchro generator
- **THEN** it satisfies `core::concepts::Logger` through the logging alias
