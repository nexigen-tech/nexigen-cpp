# core/result Specification

## Purpose

Provides the exception-free error-modeling vocabulary that every fallible `nexigen` API uses to return either a value or a classified failure, so callers can branch on an outcome without exception handling.

## Requirements

### Requirement: Error code catalogue

The system SHALL expose a fixed set of error codes in `nexigen::core`: `Ok`, `BufferFull`, `BufferEmpty`, `InvalidArgument`, `SystemError`, `NotInitialized`, `RateLimited`, and `Conflict`. Each code SHALL be a distinct, addressable enumerator usable as a non-throwing failure value. `RateLimited` classifies a rate-policy rejection before queue interaction; `Conflict` classifies an operation rejected because another owner holds a process-global resource or state (distinct from `NotInitialized`).

#### Scenario: Enumerators are available and distinct

- **WHEN** a caller names each of the eight error codes and compares any two different codes
- **THEN** all eight are usable as error values and no two compare equal

#### Scenario: Success is representable

- **WHEN** an operation completes successfully and reports its outcome as an error code
- **THEN** the value `Ok` is expressible and distinguishable from every failure code

#### Scenario: Throttle rejection is distinguishable from queue rejection

- **WHEN** a component drops a request because a rate limit was exceeded while its queue still had room
- **THEN** it can report `RateLimited`, and a caller can tell that failure apart from `BufferFull`

#### Scenario: Conflict is distinguishable from NotInitialized

- **WHEN** an operation is rejected because another owner holds a process-global resource
- **THEN** it reports `Conflict`, which a caller can tell apart from `NotInitialized`

#### Scenario: Conflict has stable value and naming

- **WHEN** `ErrorCode::Conflict` is converted to its underlying value, `to_string`, and JSON
- **THEN** the value is 7, the name is `"Conflict"`, and `from_json`/`from_string` recover the same enumerator under `JSON_NOEXCEPTION=1`

#### Scenario: Conflict formats by name

- **WHEN** `std::format("{}", ErrorCode::Conflict)` is evaluated
- **THEN** it yields `"Conflict"`

### Requirement: Compile-time error code naming

The system SHALL provide a `to_string` mapping from an error code to its human-readable name that is evaluated at compile time (`constexpr`) and returns a string view referring to static storage, with no allocation and no exception path.

#### Scenario: Name of a known code is resolvable at compile time

- **WHEN** `to_string` is applied to each of the seven defined error codes
- **THEN** each call yields that code's exact name (for example `BufferFull` yields the text `BufferFull` and `RateLimited` yields the text `RateLimited`), and the calls are valid in a constant expression

#### Scenario: Naming allocates nothing

- **WHEN** `to_string` is called on the hot path
- **THEN** no memory is allocated and no dynamic lifetime is created or destroyed

### Requirement: Result value-or-error type

The system SHALL provide a `Result<T, E = ErrorCode>` type that holds either a valid `T` value or an `E` error, and SHALL report which it holds through a query that never throws. `Result` SHALL be usable under build flags where exceptions are disabled.

#### Scenario: Constructing and reading a success

- **WHEN** a `Result<int>` is constructed from a value
- **THEN** the success query reports true and the contained value is retrievable by dereference without any throw-capable call

#### Scenario: Constructing and reading a failure

- **WHEN** a `Result<int>` is constructed from an error code such as `BufferEmpty`
- **THEN** the success query reports false and the error code is readable as the code it was constructed with

#### Scenario: Default error type

- **WHEN** a caller declares `Result<T>` without naming an error type
- **THEN** the error type is the core error code enumeration

### Requirement: Status type for operations with no value

The system SHALL provide a `Status` type representing the outcome of an operation that carries no payload — either success or an error code — and a successfully constructed `Status` SHALL report success.

#### Scenario: Successful status

- **WHEN** a `Status` is constructed to mean the operation succeeded
- **THEN** the success query reports true

#### Scenario: Failed status carries the code

- **WHEN** a `Status` is constructed from `NotInitialized`
- **THEN** the success query reports false and the readable error code is `NotInitialized`

### Requirement: Hot-path safety of the error model

The error model SHALL introduce no heap allocation, no lock, and no exception-handling requirement, and its types SHALL be usable as return values on real-time hot paths.

#### Scenario: Return-by-value of a result is allocation-free

- **WHEN** a function returns a `Result<T>` or a `Status` by value on a hot path
- **THEN** its construction, return, and inspection perform no heap allocation and no blocking call

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
