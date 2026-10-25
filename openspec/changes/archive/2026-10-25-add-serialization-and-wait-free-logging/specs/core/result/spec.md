# Spec Delta

## ADDED Requirements

### Requirement: ErrorCode name parsing

The system SHALL provide `[[nodiscard]] bool from_string(std::string_view, ErrorCode&) noexcept` in `namespace nexigen::core` that assigns the named enumerator to the out-parameter and returns `true`, or returns `false` leaving the out-parameter unchanged when the name matches no enumerator (including the `"Ok"` name of the success enumerator only if spelled exactly, and never matching `"Unknown"`).

#### Scenario: Every enumerator name parses

- **WHEN** `from_string` is called with each exact `to_string(ErrorCode)` spelling
- **THEN** it returns `true` and yields that enumerator

#### Scenario: Bogus name is rejected

- **WHEN** `from_string` is called with `"Nope"` or an empty view against a pre-set out-parameter
- **THEN** it returns `false` and the out-parameter keeps its previous value

### Requirement: ErrorCode JSON serialization

The system SHALL provide ADL functions `void to_json(nlohmann::json&, const ErrorCode&)` and `void from_json(const nlohmann::json&, ErrorCode&) noexcept` in `namespace nexigen::core` using the `to_string` name as the JSON string value; `from_json` SHALL be strictly non-throwing under `JSON_NOEXCEPTION=1`, leaving the target unchanged on a non-string value or an unknown name.

#### Scenario: Round-trip every error code

- **WHEN** each `ErrorCode` enumerator is stored via `to_json` into an `nlohmann::json` value and re-read via `from_json` into an `ErrorCode` pre-initialized to a sentinel
- **THEN** the read-back value equals the original enumerator and the JSON intermediate is a string

#### Scenario: Invalid JSON input does not terminate

- **WHEN** `from_json` receives a JSON integer, a mismatched string, or null under `-fno-exceptions`
- **THEN** the call returns normally and the `ErrorCode` target is unchanged

### Requirement: std::formatter for ErrorCode

The system SHALL specialize `std::formatter<nexigen::core::ErrorCode>` (delegating to `std::formatter<std::string_view>` over `to_string`) so error codes format directly in `std::format` and in `nexigen::logging` variadic log calls.

#### Scenario: Error code formats by name

- **WHEN** `std::format("{}", nexigen::core::ErrorCode::BufferFull)` is evaluated
- **THEN** it yields `"BufferFull"`

#### Scenario: Error code formats inside a log call

- **WHEN** a logger call such as `logger.error("failed: {}", core::ErrorCode::SystemError)` is drained
- **THEN** the emitted message text contains `"SystemError"`
