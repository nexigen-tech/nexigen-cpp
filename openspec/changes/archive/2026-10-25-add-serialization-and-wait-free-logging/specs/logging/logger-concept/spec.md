# Spec Delta

## ADDED Requirements

### Requirement: Drop reporting moves to snapshots

Because record methods no longer return a per-call `Status`, the system SHALL report every drop exclusively through logger-level snapshot surfaces (`dropped()` statistics covering rate-limited, buffer-full, and slot-exhausted causes) and the in-stream drop-summary record, so a caller never needs a per-call return value to audit losses.

#### Scenario: Silent drop is still auditable

- **WHEN** producers keep logging while one thread's shard ring is full and its calls return normally
- **THEN** `dropped()` reflects the growing buffer-full total and the drop summary appears in the sink once the window transitions

### Requirement: Format arguments reach the sink verbatim

The concept's variadic surface SHALL preserve every argument's rendered form end to end: values packed by the producer render, on the consumer, exactly as `std::format` would render them at the call site (including UTF-8 literal text and floating-point specs).

#### Scenario: Float format call survives the pipeline

- **WHEN** `logger.debug("Прямой вызов метода debug. Давление: {:.4f}", 0.0025);` is logged and drained
- **THEN** the emitted message is `Прямой вызов метода debug. Давление: 0.0025` with the call site's file, line, and function recorded

## MODIFIED Requirements

### Requirement: Logger concept core surface

The system SHALL provide C++23 concept `nexigen::logging::concepts::Logger` in namespace `nexigen::logging::concepts` that is satisfied only by types offering, without virtual dispatch: `set_level(LogLevel) noexcept`; `level() const noexcept` returning `LogLevel`; a variadic explicit-level entry `log(LogLevel, FormatWithLocation<Args...>, Args&&...) noexcept` returning `void` for every argument pack the format string accepts; and `flush() noexcept` returning `nexigen::core::Status`.

#### Scenario: Conforming type satisfies the concept

- **WHEN** `concepts::Logger<T>` is evaluated for a type offering the full required variadic surface
- **THEN** it is `true` in a constant expression

#### Scenario: Missing member rejects the concept

- **WHEN** `concepts::Logger<T>` is evaluated for a type lacking any one required member (for example `flush`)
- **THEN** it is `false`, and the rejection is detected at compile time

#### Scenario: Wrong signature rejects the concept

- **WHEN** `concepts::Logger<T>` is evaluated for a type whose `log` still returns `nexigen::core::Status` instead of `void`, or whose `level` is not `const noexcept`
- **THEN** it is `false`

### Requirement: Logger concept convenience methods

The `Logger` concept SHALL additionally require the six convenience methods `verbose`, `debug`, `info`, `warning`, `error`, and `fatal`, each variadic over `FormatWithLocation<Args...>... fmt` plus `Args&&... args`, `noexcept`, returning `void`, and each satisfying the message-only call shape (a string-literal first argument with no trailing values).

#### Scenario: Convenience set is part of the contract

- **WHEN** `concepts::Logger<T>` is evaluated for a type that provides the core surface but omits `fatal`
- **THEN** it is `false`

#### Scenario: Argument packs are accepted

- **WHEN** `t.info("count {}", 42)`-shaped calls compile on a conforming logger
- **THEN** each of the six methods likewise accepts literal-plus-arguments forms and returns `void`

### Requirement: Convenience methods carry call-site location

The six convenience methods SHALL tag each record with the declared severity level and the source location captured implicitly at the call site through the defaulted `std::source_location::current()` of the `FormatWithLocation` `consteval` constructor, so a record's file, line, and function describe the `logger.info(...)` statement itself, not any wrapper inside the library.

#### Scenario: Location comes from the caller

- **WHEN** `logger.warning("disk low at {}", level)` is called from file `app.cpp` line 42 inside function `drive()`
- **THEN** the resulting record's location fields read back as that file name, line 42, and that function name

#### Scenario: Explicit log path still available

- **WHEN** a caller invokes `log(level, "text")` directly with its own level and literal
- **THEN** the record carries exactly that level and the location of the `log` call site
