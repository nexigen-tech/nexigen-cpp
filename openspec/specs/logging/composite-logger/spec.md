# logging/composite-logger Specification

## Purpose

Provides `CompositeLogger<Loggers...>`: a variadic, template-composed fan-out logger that forwards each record to any number of heterogeneous sinks with zero virtual dispatch, while exposing compile-time and runtime access to the individual sub-loggers for configuration.

## Requirements

### Requirement: Virtual-free fan-out composition

`CompositeLogger<Loggers...>` SHALL accept zero or more types satisfying `concepts::Logger`, store them without a base class or virtual functions, and on each accepted variadic record invoke the corresponding `log`/convenience method on every member via C++23 fold expressions, forwarding the packed format arguments unchanged. The composite SHALL itself satisfy `concepts::Logger`, including for a nested `CompositeLogger`, and its record methods SHALL return `void`.

#### Scenario: Every sink receives every accepted record

- **WHEN** a `CompositeLogger<ConsoleLogger, FileLogger>` at level `Info` is called with `warning("retry {}", 3)`
- **THEN** both sub-loggers receive a record whose rendered message is `retry 3` and each applies its own level filter and sink behavior

#### Scenario: No virtual dispatch exists

- **WHEN** the composite's hot path is compiled in Release
- **THEN** no virtual table lookup appears in the generated code for forwarding (all calls are statically resolved)

#### Scenario: Composite of composites works

- **WHEN** a `CompositeLogger` is nested inside another `CompositeLogger`
- **THEN** the outer type still satisfies `concepts::Logger` and fan-out reaches all leaves
### Requirement: Level control propagation

`set_level` on a `CompositeLogger` SHALL store the level in its own relaxed atomic control and propagate it to every sub-logger so a single call re-thresholds the whole tree; `level()` SHALL report the composite's own stored level. Sub-logger-specific level control via the accessors below SHALL remain available and SHALL take effect through the sub-logger's own filter.

#### Scenario: One call moves the whole tree

- **WHEN** `set_level(LogLevel::Error)` is called on a composite of two async loggers
- **THEN** `level()` reports `Error` on the composite and both sub-loggers afterward filter below `Error`

#### Scenario: Composite short-circuit matches its own level

- **WHEN** a call below the composite's active level is issued
- **THEN** it returns immediately without invoking any sub-logger's formatting or enqueue path (bounded ≈ per-member relaxed loads, no ring traffic)
### Requirement: Sub-logger access by index and by type

`CompositeLogger` SHALL provide `get<Index>()` and `get<LoggerType>()` returning references to the corresponding stored sub-loggers, usable at compile time in constant expressions where the composite is `constexpr`, and at runtime for configuration. Requesting an index out of range or a type not present SHALL be a compile-time error, not a runtime failure.

#### Scenario: Type-based reconfiguration

- **WHEN** `composite.get<FileLogger>().set_level(LogLevel::Fatal)` is called and the composite remains at `Info`
- **THEN** `Info` records fan out but only `Fatal` records pass the file sink, while the console sink still accepts `Info`

#### Scenario: Invalid access rejected at compile time

- **WHEN** `get<NullLogger>()` is requested on a composite that does not contain a `NullLogger`, or an index beyond the pack size is used
- **THEN** the program fails to compile with a diagnosable error

#### Scenario: Empty composite is valid

- **WHEN** `CompositeLogger<>` (no members) is instantiated and logged through
- **THEN** it satisfies `concepts::Logger`, every record call returns normally with no effect, and nothing is emitted anywhere

### Requirement: Composite logger diagnostic string

`CompositeLogger<...>` SHALL provide a cold-path `std::string to_string() const` (plus free `to_string(const CompositeLogger&)`) reporting the composite's own level and member count, and embedding each member's own `to_string()` representation — a complete tree dump assembled without touching any hot-path state beyond snapshot queries.

#### Scenario: Tree dump nests member dumps

- **WHEN** `to_string()` is called on a `CompositeLogger<ConsoleLogger, FileLogger>` at level `Info`
- **THEN** the returned string names `info`, member count 2, and contains the console and file loggers' own diagnostic text

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
