# core/serialization Specification

## Purpose
Defines the project-wide serialization and formatting contract every nexigen module follows — cold-path `to_string` diagnostics, non-throwing nlohmann::json enum round-trips under `-fno-exceptions`, and `std::formatter` specializations — plus the build integration that makes the JSON vocabulary available to all consumers.

## Requirements

### Requirement: Universal cold-path to_string contract

Every public class and struct in `nexigen::core`, `nexigen::chrono`, and `nexigen::logging` SHALL provide a member `std::string to_string() const` rendering a human-readable diagnostic representation of the object's identifying fields, and a free `to_string(const T&)` that returns the same text. These functions SHALL be cold-path utilities (diagnostics, configuration, tests) and SHALL never be invoked on a real-time producer or worker hot path.

#### Scenario: Member and free forms agree

- **WHEN** `to_string(value)` is called both as member and as free function for any covered type
- **THEN** both return identical text naming the type's key fields and values

#### Scenario: Diagnostic output is descriptive

- **WHEN** `to_string() const` is called on a populated object
- **THEN** the returned string contains the object's field names or recognizable values for every field a debugging operator would need

### Requirement: Universal enum string and JSON vocabulary

Every enum class in the project SHALL provide, in its own namespace: `[[nodiscard]] constexpr std::string_view to_string(Enum) noexcept`; `[[nodiscard]] bool from_string(std::string_view, Enum&) noexcept`; `void to_json(nlohmann::json&, const Enum&)`; and `void from_json(const nlohmann::json&, Enum&) noexcept` discoverable by ADL. `to_json` SHALL emit exactly the `to_string` name; `from_json` SHALL accept exactly that name.

#### Scenario: Enum name round-trips through JSON

- **WHEN** every enumerator of a project enum is written with `to_json` and read back with `from_json`
- **THEN** each round-trip yields the original enumerator and the intermediate JSON value is the `to_string` name as a JSON string

#### Scenario: from_string accepts the canonical name

- **WHEN** `from_string` is called with the exact `to_string` spelling of an enumerator
- **THEN** it returns `true` and assigns that enumerator to the out-parameter

### Requirement: Non-throwing enum from_json under -fno-exceptions

`from_json(const nlohmann::json&, Enum&) noexcept` SHALL never terminate or abort regardless of input: when the JSON value is not a string, or the string is not a known enumerator name, the enum out-parameter SHALL be left unchanged and no exception path SHALL be taken (the JSON library is compiled with `JSON_NOEXCEPTION=1`). Callers SHALL be able to detect the rejected input by pre-initializing the target to a safe fallback value.

#### Scenario: Wrong JSON type is rejected silently

- **WHEN** `from_json` is called with a JSON number, object, array, or null against an enum pre-initialized to a fallback
- **THEN** the call returns normally and the enum still holds the fallback value

#### Scenario: Unknown name is rejected silently

- **WHEN** `from_json` is called with a JSON string that matches no enumerator
- **THEN** the call returns normally and the enum still holds its pre-call value

### Requirement: Universal enum std::formatter specialization

Every enum class SHALL specialize `template <> struct std::formatter<EnumType>` in `namespace std`, delegating to `std::formatter<std::string_view>` over `to_string(value)`, so the enum is usable directly in `std::format("{}", value)` and in `nexigen::logging` format calls without an explicit conversion.

#### Scenario: Enum formats with std::format

- **WHEN** `std::format("{}", enum_value)` is compiled and run for any project enum
- **THEN** the result equals `to_string(enum_value)`

#### Scenario: Formatter honors string_view fill and align

- **WHEN** a project enum is formatted with a width and fill/align spec (for example `[{:*>8}`)
- **THEN** the enum name is padded and aligned exactly as `std::string_view` would be

### Requirement: nlohmann_json build integration

The build SHALL obtain nlohmann::json via `find_package(nlohmann_json QUIET)` with a FetchContent fallback when the package is not installed, link it as an INTERFACE dependency reachable from `nexigen::core` (and transitively from `nexigen::chrono` and `nexigen::logging`), and publicly define `JSON_NOEXCEPTION=1` so every consumer translation unit compiles the JSON headers under the project-wide `-fno-exceptions` mandate.

#### Scenario: Configure succeeds without a system package

- **WHEN** the project is configured on a machine with no nlohmann_json installed
- **THEN** FetchContent provides the header-only dependency and configuration completes

#### Scenario: JSON headers compile without exceptions

- **WHEN** any translation unit includes a nexigen enum header and compiles with `-fno-exceptions`
- **THEN** compilation succeeds with `-Werror` because `JSON_NOEXCEPTION=1` is defined publicly
