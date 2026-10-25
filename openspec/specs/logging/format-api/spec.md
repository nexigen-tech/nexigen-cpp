# logging/format-api Specification

## Purpose
Defines `FormatWithLocation<Args...>`, the compile-time wrapper that pairs a checked `std::format_string<Args...>` with the `std::source_location` captured at the call site, giving every nexigen logger a variadic, implicitly-located, format-safe entry surface.

## Requirements

### Requirement: FormatWithLocation wrapper type

The system SHALL provide `nexigen::logging::FormatWithLocation<Args...>` holding a `std::format_string<Args...>` (pointer to the compile-time-checked literal) and a `std::source_location`, constructible `consteval` from a string-literal format with the location defaulted to `std::source_location::current()`, so a bare literal converts implicitly at any call site that takes a `FormatWithLocation`.

#### Scenario: Implicit construction from a literal with arguments

- **WHEN** `logger.debug("Прямой вызов метода debug. Давление: {:.4f}", 0.0025);` is compiled
- **THEN** the call is well-formed with no explicit wrapper, no `std::string` temporary, and no user-visible conversion

#### Scenario: Location is the caller's statement

- **WHEN** the wrapper is constructed at `app.cpp` line 42 in function `drive()`
- **THEN** its stored location's `file_name()`, `line()`, and `function_name()` describe that statement, not any library-internal frame

### Requirement: Compile-time format/argument checking

Because the wrapper's format member is a `std::format_string<Args...>` produced by a `consteval` constructor, any call whose format string does not match the supplied argument types SHALL fail to compile; no runtime format-error path can reach the logging pipeline.

#### Scenario: Mismatched replacement type is a build error

- **WHEN** code attempts `logger.info("{}", "s"sv, 42);` with a format that does not consume the arguments (or uses an invalid format spec)
- **THEN** compilation fails with a diagnostics-quality error before any logging machinery is involved

### Requirement: Wrapper adds no producer-side formatting

Constructing or passing a `FormatWithLocation` SHALL NOT invoke `std::format`, `std::format_to`, or any float-to-string conversion; the wrapper only stores the literal pointer and location values. Actual rendering is the consumer thread's responsibility (see logging/log-record).

#### Scenario: Hot path sees only storage

- **WHEN** a variadic log call is accepted on the producer in a Release build
- **THEN** the produced record contains the raw format-string pointer, the location triplet, and packed arguments — and no formatted text was generated on the producer thread
