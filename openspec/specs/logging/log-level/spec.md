# logging/log-level Specification

## Purpose

Defines the severity levels a `nexigen` logger recognizes, how a logger's active threshold is read and changed safely during execution, and the bounded-cost short-circuit that keeps calls below the threshold out of every other hot-path stage.

## Requirements

### Requirement: Severity catalogue

The system SHALL expose `nexigen::logging::LogLevel` as an enum class with exactly the enumerators `Verbose`, `Debug`, `Info`, `Warning`, `Error`, `Fatal`, `Off`, declared in strictly increasing severity order, so a relational comparison between two levels expresses severity ordering.

#### Scenario: Enumerators are distinct and severity-ordered

- **WHEN** each enumerator is compared with another of different rank
- **THEN** all seven are usable, no two compare equal, and `Verbose < Debug < Info < Warning < Error < Fatal < Off` holds at compile time

#### Scenario: Off is the highest value

- **WHEN** `Off` is compared with any other enumerator
- **THEN** `Off` compares greater, so "everything filtered" is the top-of-range state

### Requirement: Compile-time level naming

The system SHALL provide a `constexpr` mapping from `LogLevel` to its lowercase human-readable name (`"verbose"`, `"debug"`, `"info"`, `"warning"`, `"error"`, `"fatal"`, `"off"`) returning a string view over static storage, with no allocation and no exception path; the lowercase names are the spelling used in file output.

#### Scenario: Name of every level resolves at compile time

- **WHEN** the mapping is applied to each enumerator in a constant expression
- **THEN** each call yields that level's exact lowercase name and the call is valid in a constant expression

#### Scenario: Naming allocates nothing

- **WHEN** the mapping is called on a hot path
- **THEN** no memory is allocated and no dynamic lifetime is created or destroyed

### Requirement: Runtime level control

Every logger SHALL expose `set_level(LogLevel) noexcept` and `level() const noexcept -> LogLevel`, and SHALL honor a level change made at any point during execution: a relaxed atomic load of the active threshold on each log call determines filtering, and a concurrent `set_level` is visible to subsequent calls without locks or ordering handshakes.

#### Scenario: Level raised mid-run filters more

- **WHEN** a logger running at `Debug` is set to `Error` and then called with `Info`
- **THEN** the `Info` call is filtered and does not reach the output sink

#### Scenario: Level lowered mid-run admits more

- **WHEN** a logger at `Off` is set to `Verbose` and then called with `Verbose`
- **THEN** the `Verbose` call is accepted and reaches the output sink

#### Scenario: Reads and writes need no synchronization primitives

- **WHEN** one thread repeatedly reads `level()` while another repeatedly calls `set_level`
- **THEN** no mutex, condition variable, or CAS loop is used, and every observed value is one the other thread actually stored

### Requirement: Sub-threshold calls short-circuit

A log call whose level is below the active threshold, or any call while the threshold is `Off`, SHALL complete in approximately 1 ns (measured median on the target hardware in Release): the path performs at most one relaxed atomic load, one comparison, and an immediate return, touching neither a shard ring buffer, nor a timestamp source, nor the message payload or argument packing. Record methods return `void`, so filtering is simply an immediate return.

#### Scenario: Filtered call performs no logging work

- **WHEN** a call below the threshold is issued while the calling thread's shard ring is completely full
- **THEN** the call still returns immediately and enqueues nothing (the filtered path never probes the ring)

#### Scenario: Filtered call reports success

- **WHEN** a call is filtered by the level check
- **THEN** it returns normally with no drop counted — filtering is normal operation, not a failure

#### Scenario: Filtered-path latency is bounded

- **WHEN** a filtered call is issued in a measured loop in a Release build
- **THEN** its median latency is within the short-circuit budget (≈ 1 ns), materially below the accepted-record budget

### Requirement: LogLevel name parsing

The system SHALL provide `[[nodiscard]] bool from_string(std::string_view, LogLevel&) noexcept` in `namespace nexigen::logging` accepting exactly the `to_string` lowercase spellings (`"verbose"`, `"debug"`, `"info"`, `"warning"`, `"error"`, `"fatal"`, `"off"`), assigning the enumerator and returning `true`, or returning `false` with the out-parameter unchanged.

#### Scenario: Every level name round-trips with to_string

- **WHEN** each enumerator's `to_string` output is fed back through `from_string`
- **THEN** `true` is returned and the original enumerator is recovered for all seven levels

#### Scenario: Unknown spelling rejected without effects

- **WHEN** `from_string("WARN", level)` is called
- **THEN** it returns `false` and `level` is unchanged

### Requirement: LogLevel JSON serialization

The system SHALL provide ADL functions `void to_json(nlohmann::json&, const LogLevel&)` and `void from_json(const nlohmann::json&, LogLevel&) noexcept` in `namespace nexigen::logging` using the lowercase `to_string` name as the JSON string, with `from_json` strictly non-throwing under `JSON_NOEXCEPTION=1`: a non-string value or unknown name leaves the target unchanged.

#### Scenario: Level round-trips through JSON

- **WHEN** each `LogLevel` enumerator is written with `to_json` and re-read with `from_json`
- **THEN** the JSON intermediate is the lowercase name string and the re-read value is the original enumerator

#### Scenario: Hostile JSON leaves the fallback

- **WHEN** `from_json` is called with a JSON array, a number, or the string `"nope"` on a `LogLevel` pre-initialized to `LogLevel::Info`
- **THEN** the call returns normally (no terminate under `-fno-exceptions`) and the value is still `Info`

### Requirement: std::formatter for LogLevel

The system SHALL specialize `std::formatter<nexigen::logging::LogLevel>` (delegating to `std::formatter<std::string_view>` over `to_string`) so levels format directly in `std::format` and in `nexigen::logging` variadic log calls.

#### Scenario: Level formats with std::format

- **WHEN** `std::format("{}", LogLevel::Warning)` is evaluated
- **THEN** it yields `"warning"`

#### Scenario: Level formats inside a log call

- **WHEN** `logger.info("Level is {}", LogLevel::Warning)` is logged and drained
- **THEN** the emitted message text reads `Level is warning`
