# Spec Delta

## ADDED Requirements

### Requirement: Runnable composite logger example

The project SHALL provide an `ex_composite_logger` executable that composes `ConsoleLogger<>`, `FileLogger<>`, and `NullLogger` in a `CompositeLogger`, initializes the two active sinks, logs from multiple `std::jthread` producers without virtual dispatch, performs one unified flush, prints diagnostics, and exits 0.

#### Scenario: Heterogeneous sinks receive the same record

- **WHEN** the example logs an accepted record through the composite and flushes
- **THEN** the console sink and the file sink each emit the record, the null sink contributes no output, and initialization and flush report success

#### Scenario: Global level control fans out

- **WHEN** `set_level()` is called once on the composite
- **THEN** the composite and every member report the new threshold, and records below it reach no sink

#### Scenario: Per-sink configuration stays independent

- **WHEN** the file sink is raised to `Warning` through `get<FileLogger<>>()` while the composite and console remain at `Info`
- **THEN** an `Info` record appears on the console but not in the file, while a `Warning` record reaches both

#### Scenario: Both accessor forms address the same sink

- **WHEN** the example configures a sink through `get<LoggerType>()` and reads it back through `get<Index>()`
- **THEN** both references address the same stored logger instance and observe the same level

#### Scenario: Composite diagnostics render

- **WHEN** the example prints the composite's `to_string()`
- **THEN** the text reports the composite level, member count, and each member's own diagnostic text
