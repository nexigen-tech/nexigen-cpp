# Spec Delta

## ADDED Requirements

### Requirement: Runnable console logger example

The project SHALL provide an `ex_console_logger` executable that initializes a default `ConsoleLogger`, logs from multiple `std::jthread` producers through the six variadic convenience methods, flushes, prints diagnostics, and exits 0.

#### Scenario: Multi-threaded console logging completes

- **WHEN** the example starts several worker threads that each log messages at every severity and then flushes
- **THEN** initialization and flush report success without using `.value()`, every accepted message is emitted by the writer, and the process exits 0

#### Scenario: Runtime level filtering is visible

- **WHEN** the example raises the threshold with `set_level()` and then logs below and at the new threshold
- **THEN** records below the threshold produce no line, records at or above it are printed, and the logger reports the new level

#### Scenario: Levels format directly

- **WHEN** the example formats a `LogLevel` value with `std::format` and logs it as an argument
- **THEN** the output contains the canonical lowercase severity name

#### Scenario: Console diagnostics render

- **WHEN** the example prints `dropped().to_string()` and the logger's `to_string()` after flushing
- **THEN** the text reports the drop counters and the logger's level, capacity, pending count, and drop totals
