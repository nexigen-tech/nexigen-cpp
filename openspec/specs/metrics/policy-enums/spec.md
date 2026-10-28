# metrics/policy-enums Specification

## Purpose

Defines the policy vocabulary shared by every metrics instrument — concurrency, access, reset, and metric-kind classifications — together with the module-wide diagnostic-string, `nlohmann::json`, and `std::format` contract every public metrics type follows.

## Requirements

### Requirement: Policy enum catalogue

`nexigen::metrics` SHALL define four distinct scoped enum types with a fixed `std::uint8_t` underlying type: `ConcurrencyPolicy` (`SingleThreaded`, `MultiThreaded`), `AccessPolicy` (`Readable`, `WriteOnly`), `ResetPolicy` (`Cumulative`, `ResetOnRead`), and `MetricKind` (`Counter`, `Gauge`, `DescriptiveStatistics`, `ExplicitHistogram`, `ExponentialHistogram`). Numeric values are append-only and stable.

#### Scenario: Enumerator sets are complete

- **WHEN** each policy enum type is inspected at compile time
- **THEN** it contains exactly the enumerators listed above, in that order, with consecutive values starting at zero

#### Scenario: Enums are distinct and byte-sized

- **WHEN** the four types are compared and sized in a constant expression
- **THEN** no two types are the same type, each has underlying type `std::uint8_t`, and `sizeof` each is `1`

### Requirement: Constexpr policy names

Each policy enum SHALL provide `[[nodiscard]] constexpr std::string_view to_string(Enum) noexcept` returning the exact PascalCase enumerator spelling, and `[[nodiscard]] constexpr bool from_string(std::string_view, Enum&) noexcept` that accepts only that canonical spelling and leaves the out-parameter unchanged on failure.

#### Scenario: ConcurrencyPolicy name mapping

- **WHEN** `to_string` is called with `SingleThreaded` and `MultiThreaded`, and `from_string` is called with `"SingleThreaded"`, `"MultiThreaded"`, and `"singlethreaded"`
- **THEN** the renders are `"SingleThreaded"` and `"MultiThreaded"`, the first two parses succeed, and the lowercase parse returns `false` without modifying the output value

#### Scenario: AccessPolicy name mapping

- **WHEN** `to_string` is called with `Readable` and `WriteOnly`, and `from_string` is called with `"Readable"`, `"WriteOnly"`, and `"Unknown"`
- **THEN** the renders are `"Readable"` and `"WriteOnly"`, the first two parses succeed, and `"Unknown"` returns `false` without modifying the output value

#### Scenario: ResetPolicy name mapping

- **WHEN** `to_string` is called with `Cumulative` and `ResetOnRead`, and `from_string` is called with `"Cumulative"`, `"ResetOnRead"`, and an empty view
- **THEN** the renders are `"Cumulative"` and `"ResetOnRead"`, the first two parses succeed, and the empty view returns `false` without modifying the output value

#### Scenario: MetricKind name mapping

- **WHEN** `to_string` is called with every `MetricKind` enumerator and each result is parsed back with `from_string`
- **THEN** the renders are exactly `"Counter"`, `"Gauge"`, `"DescriptiveStatistics"`, `"ExplicitHistogram"`, and `"ExponentialHistogram"`, and each round-trips

#### Scenario: Unknown values render safely

- **WHEN** `to_string` receives an out-of-range value obtained by casting an integer outside the enumerator set
- **THEN** it returns the static-storage view `"Unknown"` without throwing or allocating

### Requirement: Non-throwing policy JSON serialization

Each policy enum SHALL provide ADL-discoverable `to_json(nlohmann::json&, const Enum&)` writing the canonical `to_string` value as a JSON string, and `from_json(const nlohmann::json&, Enum&) noexcept` reading a string via `get_ptr` and `from_string`. Neither function SHALL throw or abort under `JSON_NOEXCEPTION=1` and `-fno-exceptions`.

#### Scenario: Policy JSON round-trips

- **WHEN** `MultiThreaded`, `WriteOnly`, `ResetOnRead`, and `ExponentialHistogram` are each serialized to JSON and parsed back
- **THEN** each JSON value is the canonical name string and each parsed enum equals its input

#### Scenario: Invalid JSON is rejected without aborting

- **WHEN** `from_json` receives a JSON number, object, array, `null`, or an unrecognized string
- **THEN** it returns normally, emits no exception or abort under `JSON_NOEXCEPTION=1`, and leaves the enum at its prior value

### Requirement: Policy enum formatting

Each policy enum SHALL specialize `std::formatter<Enum>` in `namespace std`, delegating to `std::formatter<std::string_view>` over `to_string`, so `std::format` renders the canonical name while honoring fill, alignment, and width options.

#### Scenario: Policy enums format by canonical name

- **WHEN** `std::format("{}", ...)` is evaluated for `ConcurrencyPolicy::SingleThreaded`, `AccessPolicy::Readable`, `ResetPolicy::Cumulative`, and `MetricKind::DescriptiveStatistics`
- **THEN** the results are `"SingleThreaded"`, `"Readable"`, `"Cumulative"`, and `"DescriptiveStatistics"`

#### Scenario: Width and alignment are honored

- **WHEN** a policy enum is formatted with a width and alignment specifier
- **THEN** the output applies the requested field width and alignment around the canonical name

### Requirement: Public type diagnostic strings

Every public class, struct, snapshot, and configuration type published by `nexigen::metrics` SHALL provide a cold-path `std::string to_string() const` and an ADL-discoverable free `to_string(const T&)` returning the same representation. These functions may allocate and are diagnostics-only.

#### Scenario: Member and free functions agree

- **WHEN** any public metrics type instance is passed to the free `to_string` and its member `to_string()` is called
- **THEN** both return equal, non-empty strings

#### Scenario: Serialization stays off the hot path

- **WHEN** the metrics hot-path headers are inspected
- **THEN** no `to_string()` call, `std::string` construction, or JSON operation appears in recording, reading, or harvesting code paths

### Requirement: Public type JSON serialization

Every public metrics value type SHALL provide non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. Types that cannot be reconstructed outside the factory hierarchy — instrument classes and variadic composite sinks — SHALL provide `to_json` and deliberately no `from_json`, so attempted deserialization fails at compile time.

#### Scenario: Value-type JSON round-trips

- **WHEN** any public configuration, record, or snapshot type is serialized with `to_json` and parsed back with `from_json`
- **THEN** every field survives the round-trip under `JSON_NOEXCEPTION=1`

#### Scenario: Factory-only types are not deserializable

- **WHEN** a translation unit attempts `from_json` into an instrument class or a `CompositeMetricSink` instantiation
- **THEN** compilation fails because no overload exists, preserving the Meter-only creation rule

#### Scenario: Malformed JSON never aborts

- **WHEN** `from_json` on any metrics value type receives structurally wrong input
- **THEN** it returns normally with fields left at their prior values and no abort under `-fno-exceptions`

### Requirement: Public type formatting

Every public metrics type SHALL be usable in `std::format` through a `std::formatter` specialization that renders its `to_string()` text, so metrics values format directly in tests and diagnostics.

#### Scenario: Class instances format as their diagnostic string

- **WHEN** `std::format("{}", value)` is evaluated for an instance of any public metrics class or struct
- **THEN** the result equals that instance's `to_string()` text

#### Scenario: Formatting remains cold path

- **WHEN** formatting is exercised in the test suite
- **THEN** it is invoked only from diagnostic and serialization code paths, never from recording or harvesting loops
