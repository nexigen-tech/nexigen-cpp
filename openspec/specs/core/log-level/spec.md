# core/log-level Specification

## Purpose

Defines the program-wide severity vocabulary: the `nexigen::core::LogLevel` catalogue every module uses for threshold-based filtering without reaching into `nexigen::logging`, together with its compile-time naming, simple-name parsing, JSON serialization, and direct format-ability. The vocabulary carries no behavior of its own beyond the intangible catalogue protocol; logger behavior built on it remains in the logging capabilities.

## Requirements

### Requirement: Severity catalogue

The system SHALL expose `nexigen::core::LogLevel` as an enum class with exactly the enumerators `Verbose`, `Debug`, `Info`, `Warning`, `Error`, `Fatal`, `Off`, declared in strictly increasing severity order, so a relational comparison between two levels expresses severity ordering.

#### Scenario: Enumerators are distinct and severity-ordered

- **WHEN** each enumerator is compared with another of different rank
- **THEN** all seven are usable, no two compare equal, and `Verbose < Debug < Info < Warning < Error < Fatal < Off` holds at compile time

#### Scenario: Off is the highest value

- **WHEN** `Off` is compared with any other enumerator
- **THEN** `Off` compares greater, so "everything filtered" is the top-of-range state

### Requirement: Compile-time level naming

The system SHALL provide a `constexpr` mapping from `nexigen::core::LogLevel` to its lowercase human-readable name (`"verbose"`, `"debug"`, `"info"`, `"warning"`, `"error"`, `"fatal"`, `"off"`) returning a string view over static storage, with no allocation and no exception path; the lowercase names are the spelling used in file output.

#### Scenario: Name of every level resolves at compile time

- **WHEN** the mapping is applied to each enumerator in a constant expression
- **THEN** each call yields that level's exact lowercase name and the call is valid in a constant expression

#### Scenario: Naming allocates nothing

- **WHEN** the mapping is called on a hot path
- **THEN** no memory is allocated and no dynamic lifetime is created or destroyed

### Requirement: LogLevel name parsing

The system SHALL provide `[[nodiscard]] constexpr bool from_string(std::string_view, LogLevel&) noexcept` in `namespace nexigen::core` accepting exactly the `to_string` lowercase spellings (`"verbose"`, `"debug"`, `"info"`, `"warning"`, `"error"`, `"fatal"`, `"off"`), assigning the enumerator and returning `true`, or returning `false` with the out-parameter unchanged.

#### Scenario: Every level name round-trips with to_string

- **WHEN** each enumerator's `to_string` output is fed back through `from_string`
- **THEN** `true` is returned and the original enumerator is recovered for all seven levels

#### Scenario: Unknown spelling rejected without effects

- **WHEN** `from_string("WARN", level)` is called
- **THEN** it returns `false` and `level` is unchanged

### Requirement: LogLevel JSON serialization

The system SHALL provide ADL functions `void to_json(nlohmann::json&, const LogLevel&)` and `void from_json(const nlohmann::json&, LogLevel&) noexcept` in `namespace nexigen::core` using the lowercase `to_string` name as the JSON string, with `from_json` strictly non-throwing under `JSON_NOEXCEPTION=1`: a non-string value or unknown name leaves the target unchanged.

#### Scenario: Level round-trips through JSON

- **WHEN** each `LogLevel` enumerator is written with `to_json` and re-read with `from_json`
- **THEN** the JSON intermediate is the lowercase name string and the re-read value is the original enumerator

#### Scenario: Hostile JSON leaves the fallback

- **WHEN** `from_json` is called with a JSON array, a number, or the string `"nope"` on a `LogLevel` pre-initialized to `LogLevel::Info`
- **THEN** the call returns normally (no terminate under `-fno-exceptions`) and the value is still `Info`

### Requirement: std::formatter for LogLevel

The system SHALL make `nexigen::core::LogLevel` directly formattable in `std::format` as its canonical lowercase name (delegating to `std::formatter<std::string_view>` over `to_string`, with parse and fill/align handled by the string-view formatter rules), so levels format directly in `std::format` and in the logging variadic log calls.

#### Scenario: Level formats with std::format

- **WHEN** `std::format("{}", LogLevel::Warning)` is evaluated
- **THEN** it yields `"warning"`

#### Scenario: Level formats inside a log call

- **WHEN** `logger.info("Level is {}", LogLevel::Warning)` is logged and drained
- **THEN** the emitted message text reads `Level is warning`
