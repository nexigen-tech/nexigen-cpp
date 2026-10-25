# Spec Delta

## ADDED Requirements

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

## MODIFIED Requirements

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
