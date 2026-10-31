# Spec Delta

## Purpose

Identifies the base clock domains present in every Linux system and provides a compile-time domain declaration so a component can report which clock it stamps in without per-record tags.

## ADDED Requirements

### Requirement: Base clock-domain identifiers

`nexigen::chrono::Clock_id` SHALL be a closed enum enumerating the base clock domains available on every Linux system: `monotonic_raw` (the raw, non-frequency-disciplined monotonic clock), `boottime` (monotonic including suspend), and `realtime` (the wall clock disciplined by NTP/PTP). Each value SHALL round-trip through `to_string`/`from_string`, `to_json`/`from_json`, and `std::format`.

#### Scenario: Enum enumerates the base domains

- **WHEN** `Clock_id` is inspected at compile time
- **THEN** it enumerates exactly `monotonic_raw`, `boottime`, and `realtime`

#### Scenario: to_string and from_string round-trip

- **WHEN** each `Clock_id` value is converted to a string with `to_string` and parsed back with `from_string`
- **THEN** the recovered value equals the original, and `to_string` is a `constexpr std::string_view`

#### Scenario: Unknown string is rejected

- **WHEN** `from_string` is called with an unrecognized string
- **THEN** it returns `false` and leaves the output value unchanged

#### Scenario: JSON round-trip under no exceptions

- **WHEN** a `Clock_id` is serialized with `to_json` and parsed back with `from_json` under `JSON_NOEXCEPTION=1`
- **THEN** the recovered value equals the original, and a non-string JSON value is rejected without throwing

#### Scenario: Formatter matches to_string

- **WHEN** `std::format("{}", Clock_id::monotonic_raw)` is evaluated
- **THEN** the result equals `to_string(Clock_id::monotonic_raw)`

### Requirement: Compile-time domain declaration

A component SHALL be able to declare the clock domain it stamps in as a compile-time constant, so that domain is discoverable without runtime state, without an object instance, and without per-record tags. A component that does not declare a domain SHALL be treated as `monotonic_raw`.

#### Scenario: Declaration is a constant expression

- **WHEN** the declared domain of a component is evaluated
- **THEN** it is a `constexpr Clock_id` and does not require constructing or holding a live object

#### Scenario: Querying the declaration is free

- **WHEN** the declared domain is read under an armed allocation arena
- **THEN** no allocation, lock, or clock read occurs

#### Scenario: Undeclared defaults to the canonical hot domain

- **WHEN** a component provides no domain declaration
- **THEN** its domain is interpreted as `monotonic_raw`
