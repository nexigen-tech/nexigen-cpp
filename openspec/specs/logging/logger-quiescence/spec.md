# logging/logger-quiescence Specification

## Purpose

Defines how a logger is stopped for good: after a final flush its consumer halts and its sink closes, the logger object and its accounting stay alive, and later records are dropped without blocking producers.

## Requirements

### Requirement: Quiescable logger contract

The logging module SHALL provide a concept `QuiescableLogger` satisfied by a logger that additionally offers `quiesce() noexcept` returning `core::Status`. `NullLogger`, `ConsoleLogger`, `FileLogger`, `Iceoryx2Logger`, and a `CompositeLogger` over conforming members SHALL satisfy it and pin it with a compile-time assertion.

#### Scenario: Conforming logger satisfies the concept

- **WHEN** `QuiescableLogger<T>` is evaluated for a logger offering the record surface and a `quiesce()` returning `core::Status`
- **THEN** it is `true` in a constant expression

#### Scenario: Missing quiesce rejects the concept

- **WHEN** `QuiescableLogger<T>` is evaluated for a type lacking `quiesce`
- **THEN** it is `false` and the rejection is detected at compile time

#### Scenario: Published loggers are pinned

- **WHEN** the test translation unit is compiled
- **THEN** the assertion passes for every published logger, and removing `quiesce` from any of them fails the build

#### Scenario: Composite quiesces every member

- **WHEN** `quiesce()` is called on a `CompositeLogger`
- **THEN** each member logger is quiesced and the first member failure is returned

### Requirement: Quiescence stops the consumer and keeps the logger alive

`quiesce()` SHALL flush pending records, stop and join the background consumer, close the sink, and keep the logger object, its rings, and its drop accounting alive. It SHALL NOT release storage, allocate, or throw.

#### Scenario: Pending records are delivered

- **WHEN** records are enqueued and `quiesce()` is called
- **THEN** every record accepted before the call reaches the sink before the sink closes

#### Scenario: The sink is closed once

- **WHEN** a file logger is quiesced
- **THEN** its file is closed during quiescence and no later record reaches it

#### Scenario: No allocation on the quiescence path

- **WHEN** `quiesce()` runs with the allocation counter armed
- **THEN** no `operator new`, `malloc`, or container growth is observed

#### Scenario: The logger object stays usable for diagnostics

- **WHEN** `to_string()` and `dropped()` are called after `quiesce()`
- **THEN** both return their normal content and no memory error occurs

### Requirement: Quiescence is terminal and idempotent

A quiesced logger SHALL NOT be restarted by the quiescence contract, a repeated `quiesce()` SHALL report success without repeating the drain, and `flush()` after quiescence SHALL return `NotInitialized`.

#### Scenario: Repeated quiesce is harmless

- **WHEN** `quiesce()` is called twice
- **THEN** the second call returns success, the sink is not contacted again, and no sanitizer reports a problem

#### Scenario: Flush after quiescence reports NotInitialized

- **WHEN** `flush()` is called on a quiesced logger
- **THEN** it returns `ErrorCode::NotInitialized`

### Requirement: Post-quiescence records are dropped without blocking

After quiescence, record methods SHALL remain callable and SHALL never block. They SHALL deliver nothing to the sink, and records the ring can no longer accept SHALL be discarded and counted by the existing overflow policy.

#### Scenario: Producers keep logging after quiescence

- **WHEN** a producer calls `info()` repeatedly after `quiesce()` with a full ring
- **THEN** every call returns without blocking

#### Scenario: Losses stay auditable

- **WHEN** enough post-quiescence records are produced to fill the ring
- **THEN** the sink receives none, `dropped()` grows, and the drop summary is synthesized only when a consumer exists
