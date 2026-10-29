# Spec Delta

## Purpose

Defines the `nexigen::diagnostics` value vocabulary: the `StopwatchState` lifecycle enum and the module-wide serialization contract (`constexpr` names, non-throwing nlohmann JSON under `-fno-exceptions`, `std::formatter`, and cold-path diagnostic `to_string()`) applied to every public diagnostics type.

## ADDED Requirements

### Requirement: StopwatchState enumerates the stopwatch lifecycle

`nexigen::diagnostics` SHALL describe module state with the scoped enum `StopwatchState` holding exactly `Idle`, `Running`, `Stopped`. It SHALL complete constexpr name conversion: `to_string(StopwatchState) noexcept -> std::string_view` returning the PascalCase spelling (`"Idle"`, `"Running"`, `"Stopped"`, unknown `"Unknown"`), `from_string(std::string_view, StopwatchState&) noexcept -> bool` parsing those spellings exactly, returning `false` with the out-parameter unchanged otherwise.

#### Scenario: Every enumerator round-trips

- **WHEN** each of `Idle`, `Running`, `Stopped` is converted by `to_string` and back by `from_string`
- **THEN** `to_string` yields the enumerator's exact PascalCase name and the round-trip reproduces the original enumerator

#### Scenario: Unknown input is rejected without mutation

- **WHEN** `from_string` is called with a misspelled ("running "), differently cased ("idle"), or empty name
- **THEN** it returns `false`, leaves the out-parameter at its prior value, and never exits the process

#### Scenario: Garbage-cast values stay well-defined

- **WHEN** a `StopwatchState` is built by casting a value that is not an enumerator and passed to `to_string`
- **THEN** the call returns normally with the `"Unknown"` rendering and no UB (the switch carries a `default:` arm)

### Requirement: StopwatchState serializes to nlohmann JSON without throwing

With `JSON_NOEXCEPTION=1` (the project compiles everything under `-fno-exceptions`), `StopwatchState` SHALL provide ADL-discoverable `to_json(nlohmann::json&, const StopwatchState&)` writing the canonical name string, and `from_json(const nlohmann::json&, StopwatchState&) noexcept` reading it back: non-string values and unknown names SHALL leave the out-parameter unchanged, and no call path may throw or abort.

#### Scenario: JSON round-trip preserves the state

- **WHEN** a value is written with `to_json` and read back with `from_json`
- **THEN** the JSON value is the canonical name string and the reconstructed enum equals the original

#### Scenario: Non-string and malformed JSON are silently rejected

- **WHEN** `from_json` is applied to a number, null, boolean, or a string that is not a canonical state name
- **THEN** the out-parameter keeps its prior value, and the call completes without throwing under `JSON_NOEXCEPTION`

### Requirement: StopwatchState formats through std::format

`StopwatchState` SHALL be directly formattable via a `std::formatter` specialization defined in `namespace std` that delegates to the canonical name (fill/align/parsing handled by the `std::string_view` formatter), so it renders inside `std::format` and `nexigen::logging` format calls.

#### Scenario: Direct formatting works without conversion

- **WHEN** `std::format("{}", StopwatchState::Running)` is evaluated
- **THEN** the result equals the canonical name `"Running"`

#### Scenario: Fill and alignment apply

- **WHEN** the value is formatted with width/alignment specifications such as `{:>8}`
- **THEN** the canonical name is padded per the standard string-view formatting rules

### Requirement: Diagnostics types render cold-path diagnostics

Every public class and struct in `nexigen::diagnostics` — `Stopwatch`, `LapSnapshot`, and `ScopedStopwatch` — SHALL provide a cold-path member `std::string to_string() const` and an ADL free `to_string(const T&)` mirroring the member; these allocate a `std::string` and are diagnostics/config utilities that SHALL NOT be called on real-time hot paths. Enum members of diagnostic strings ride `to_string(StopwatchState)`.

#### Scenario: Each diagnostics type mirrors its member with a free function

- **WHEN** the free `to_string(const Stopwatch&)`, `to_string(const LapSnapshot&)`, and `to_string(const ScopedStopwatch&)` are evaluated on live objects
- **THEN** each returns exactly the text of the corresponding member `to_string()`

#### Scenario: Rendering reports live state

- **WHEN** `to_string()` is rendered on each diagnostics type in an active measurement
- **THEN** the text identifies the type, its state/counters (state name, elapsed time, lap number/count, or guard report as applicable), and contains no JSON escapes
