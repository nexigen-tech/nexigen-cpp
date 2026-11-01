# Spec Delta

## MODIFIED Requirements

### Requirement: Policy enum catalogue

`nexigen::metrics` SHALL define four distinct scoped enum types with a fixed `std::uint8_t` underlying type: `ConcurrencyPolicy` (`SingleThreaded`, `MultiThreaded`), `AccessPolicy` (`Readable`, `WriteOnly`), `ResetPolicy` (`Cumulative`, `ResetOnRead`), and `MetricKind` (`Counter`, `Gauge`, `DescriptiveStatistics`, `ExplicitHistogram`, `ExponentialHistogram`, `State`). Numeric values are append-only and stable.

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
- **THEN** the renders are exactly `"Counter"`, `"Gauge"`, `"DescriptiveStatistics"`, `"ExplicitHistogram"`, `"ExponentialHistogram"`, and `"State"`, and each round-trips

#### Scenario: Unknown values render safely

- **WHEN** `to_string` receives an out-of-range value obtained by casting an integer outside the enumerator set
- **THEN** it returns the static-storage view `"Unknown"` without throwing or allocating
