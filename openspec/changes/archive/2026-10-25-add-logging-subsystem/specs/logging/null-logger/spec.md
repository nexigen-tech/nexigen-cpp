# Spec Delta

## Purpose

Provides the zero-cost logging sink — a conforming logger that discards every record without observable work — so release builds and benchmark harnesses can keep the full `Logger` call shape compiled down to nothing.

## ADDED Requirements

### Requirement: Conforming no-op logger

The system SHALL provide `NullLogger` satisfying `concepts::Logger` with no state beyond its level control: every record method (`log` and the six convenience methods) SHALL return an OK `Status` without emitting output, touching a ring buffer, or allocating, and `flush` SHALL return OK immediately.

#### Scenario: NullLogger satisfies the concept

- **WHEN** `concepts::Logger<NullLogger>` is evaluated
- **THEN** it is `true` in a constant expression

#### Scenario: Every call succeeds silently

- **WHEN** records at any level, including `Fatal`, and `flush` are invoked on a `NullLogger`
- **THEN** every call returns an OK `Status` and no output appears on any stream or file

#### Scenario: Level control is still honored

- **WHEN** `set_level(LogLevel::Warning)` is called on a `NullLogger`
- **THEN** `level()` reports `Warning`, and calls below it short-circuit like any other logger

### Requirement: Calls compile away

At optimization levels `-O1` and above, a call to a `NullLogger` method whose result is used ONLY in its OK-ness SHALL reduce to no emitted work: no function call survives into the generated code for the filtered/accepted paths when the object is visible to the optimizer.

#### Scenario: Optimized loop leaves no logging residue

- **WHEN** a tight loop calling `NullLogger::info(...)` is compiled in Release with inlining enabled
- **THEN** the loop body contains no surviving call into the logging subsystem and the loop runs at the cost of its non-logging statements
