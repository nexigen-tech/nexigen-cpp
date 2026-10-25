# logging/null-logger Specification

## Purpose

Provides the zero-cost logging sink — a conforming logger that discards every record without observable work — so release builds and benchmark harnesses can keep the full `Logger` call shape compiled down to nothing.

## Requirements

### Requirement: Conforming no-op logger

The system SHALL provide `NullLogger` satisfying `concepts::Logger` with no state beyond its level control: every record method (the variadic `log` and the six convenience methods) SHALL return from accepting or filtering the call without emitting output, touching a ring buffer, or allocating, and `flush` SHALL return OK immediately.

#### Scenario: NullLogger satisfies the concept

- **WHEN** `concepts::Logger<NullLogger>` is evaluated
- **THEN** it is `true` in a constant expression

#### Scenario: Every call succeeds silently

- **WHEN** records at any level, including `Fatal` and calls with format arguments, and `flush` are invoked on a `NullLogger`
- **THEN** every record call returns normally with no output appearing on any stream or file, and `flush` returns OK

#### Scenario: Level control is still honored

- **WHEN** `set_level(LogLevel::Warning)` is called on a `NullLogger`
- **THEN** `level()` reports `Warning`, and calls below it short-circuit like any other logger
### Requirement: Calls compile away

At optimization levels `-O1` and above, a call to a `NullLogger` record method SHALL reduce to no emitted work: no function call survives into the generated code for the filtered/accepted paths when the object is visible to the optimizer, so keeping the full variadic `Logger` call shape costs nothing.

#### Scenario: Optimized loop leaves no logging residue

- **WHEN** a tight loop calling `NullLogger::info("tick {}", n)` is compiled in Release with inlining enabled
- **THEN** the loop body contains no surviving call into the logging subsystem and the loop runs at the cost of its non-logging statements

### Requirement: NullLogger diagnostic string

`NullLogger` SHALL provide a cold-path `std::string to_string() const` (member, plus free `to_string(const NullLogger&)`) reporting its stored level — the complete state of the sink — for diagnostics and tests, never called on a hot path.

#### Scenario: Dump names the threshold

- **WHEN** `to_string()` is called after `set_level(LogLevel::Warning)`
- **THEN** the returned string contains `warning`
