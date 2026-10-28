# tracing/span-enums Specification

## Purpose

Defines the span vocabulary enums — kind, status, and sampling decision — together with the module-wide diagnostic-string, `nlohmann::json`, and `std::format` contract every public tracing type follows.

## Requirements

### Requirement: Span enum catalogue

`nexigen::tracing` SHALL define three distinct scoped enum types with a fixed `std::uint8_t` underlying type: `SpanKind` (`Internal`, `Server`, `Client`, `Producer`, `Consumer`), `SpanStatus` (`Unset`, `Ok`, `Error`), and `SamplingDecision` (`Drop`, `RecordAndSample`). Numeric values are append-only and stable.

#### Scenario: Enumerator sets are complete

- **WHEN** each span enum type is inspected at compile time
- **THEN** it contains exactly the enumerators listed above, in that order, with consecutive values starting at zero

#### Scenario: Enums are distinct and byte-sized

- **WHEN** the three types are compared and sized in a constant expression
- **THEN** no two types are the same type, each has underlying type `std::uint8_t`, and `sizeof` each is `1`

### Requirement: Constexpr span enum names

Each span enum SHALL provide `[[nodiscard]] constexpr std::string_view to_string(Enum) noexcept` returning the exact enumerator spelling (`"Internal"`, `"Unset"`, `"RecordAndSample"`, ...), and `[[nodiscard]] constexpr bool from_string(std::string_view, Enum&) noexcept` that accepts only that canonical spelling and leaves the out-parameter unchanged on failure.

#### Scenario: SpanKind name mapping

- **WHEN** `to_string` is called with every `SpanKind` enumerator and each result is parsed back with `from_string`
- **THEN** the renders are exactly `"Internal"`, `"Server"`, `"Client"`, `"Producer"`, and `"Consumer"`, and each round-trips

#### Scenario: SpanStatus name mapping

- **WHEN** `to_string` is called with `Unset`, `Ok`, and `Error`, and `from_string` is called with `"Unset"`, `"Ok"`, `"Error"`, and `"ok"`
- **THEN** the renders are `"Unset"`, `"Ok"`, and `"Error"`, the first three parses succeed, and the lowercase parse returns `false` without modifying the output value

#### Scenario: SamplingDecision name mapping

- **WHEN** `to_string` is called with `Drop` and `RecordAndSample`, and `from_string` is called with `"Drop"`, `"RecordAndSample"`, and an empty view
- **THEN** the renders are `"Drop"` and `"RecordAndSample"`, the first two parses succeed, and the empty view returns `false` without modifying the output value

#### Scenario: Unknown values render safely

- **WHEN** `to_string` receives an out-of-range value obtained by casting an integer outside the enumerator set
- **THEN** it returns the static-storage view `"Unknown"` without throwing or allocating

### Requirement: Non-throwing span enum JSON serialization

Each span enum SHALL provide ADL-discoverable `to_json(nlohmann::json&, const Enum&)` writing the canonical `to_string` value as a JSON string, and `from_json(const nlohmann::json&, Enum&) noexcept` reading a string via `get_ptr` and `from_string`. Neither function SHALL throw or abort under `JSON_NOEXCEPTION=1` and `-fno-exceptions`.

#### Scenario: Span enum JSON round-trips

- **WHEN** `Client`, `Error`, and `RecordAndSample` are each serialized to JSON and parsed back
- **THEN** each JSON value is the canonical name string and each parsed enum equals its input

#### Scenario: Invalid JSON is rejected without aborting

- **WHEN** `from_json` receives a JSON number, object, array, `null`, or an unrecognized string on a `SpanStatus` pre-initialized to `Unset`
- **THEN** the call returns normally, emits no exception or abort under `JSON_NOEXCEPTION=1`, and the value is still `Unset`

### Requirement: Span enum formatting

Each span enum SHALL specialize `std::formatter<Enum>` in `namespace std`, delegating to `std::formatter<std::string_view>` over `to_string`, so `std::format` renders the canonical name while honoring fill, alignment, and width options.

#### Scenario: Span enums format by canonical name

- **WHEN** `std::format("{}", ...)` is evaluated for `SpanKind::Server`, `SpanStatus::Error`, and `SamplingDecision::Drop`
- **THEN** the results are `"Server"`, `"Error"`, and `"Drop"`

#### Scenario: Width and alignment are honored

- **WHEN** a span enum is formatted with a width and alignment specifier
- **THEN** the output applies the requested field width and alignment around the canonical name

### Requirement: Tracing-wide type diagnostic strings

Every public class, struct, record, and configuration type published by `nexigen::tracing` SHALL provide a cold-path `std::string to_string() const` and an ADL-discoverable free `to_string(const T&)` returning the same representation. These functions may allocate and are diagnostics-only.

#### Scenario: Member and free functions agree

- **WHEN** any public tracing type instance is passed to the free `to_string` and its member `to_string()` is called
- **THEN** both return equal, non-empty strings

#### Scenario: Serialization stays off the hot path

- **WHEN** the tracing hot-path headers are inspected
- **THEN** no `to_string()` call, `std::string` construction, or JSON operation appears in span start, attribute, status, or enqueue code paths

### Requirement: Tracing-wide type JSON and format surfaces

Every public tracing value type SHALL provide non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`; every public tracing type SHALL be usable in `std::format` through a `std::formatter` specialization rendering its `to_string()` text. Types that cannot be reconstructed outside the factory hierarchy — live guards, provider/tracer handles, and variadic composite sinks — SHALL provide `to_json` and deliberately no `from_json`, so attempted deserialization fails at compile time.

#### Scenario: Value-type JSON round-trips

- **WHEN** any public tracing configuration, context, attribute, or record type is serialized with `to_json` and parsed back with `from_json`
- **THEN** every field survives the round-trip under `JSON_NOEXCEPTION=1`

#### Scenario: Factory-bound types are not deserializable

- **WHEN** a translation unit attempts `from_json` into a `ScopedSpan`, `Tracer`, `TracerProvider`, or a `CompositeTraceSink` instantiation
- **THEN** compilation fails because no overload exists, preserving the factory-only creation rule

#### Scenario: Class instances format as their diagnostic string

- **WHEN** `std::format("{}", value)` is evaluated for an instance of any public tracing class or struct
- **THEN** the result equals that instance's `to_string()` text

#### Scenario: Malformed JSON never aborts

- **WHEN** `from_json` on any public tracing value type receives structurally wrong input
- **THEN** it returns normally with fields left at their prior values and no abort under `-fno-exceptions`
