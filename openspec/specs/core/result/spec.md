# core/result Specification

## Purpose

Provides the exception-free error-modeling vocabulary that every fallible `nexigen` API uses to return either a value or a classified failure, so callers can branch on an outcome without exception handling.

## Requirements

### Requirement: Error code catalogue

The system SHALL expose a fixed set of error codes in `nexigen::core` covering the outcome of an operation: `Ok`, `BufferFull`, `BufferEmpty`, `InvalidArgument`, `SystemError`, `NotInitialized`, and `RateLimited`. Each code SHALL be a distinct, addressable enumerator usable as a non-throwing failure value, and the set SHALL be exhaustive for the failure classifications the core layer reports. `RateLimited` classifies a request rejected by a rate policy before any queue interaction (distinct from `BufferFull`, which classifies bounded-queue rejection at capacity).

#### Scenario: Enumerators are available and distinct

- **WHEN** a caller names each of the seven error codes and compares any two different codes
- **THEN** all seven are usable as error values and no two compare equal

#### Scenario: Success is representable

- **WHEN** an operation completes successfully and reports its outcome as an error code
- **THEN** the value `Ok` is expressible and distinguishable from every failure code

#### Scenario: Throttle rejection is distinguishable from queue rejection

- **WHEN** a component drops a request because a rate limit was exceeded while its queue still had room
- **THEN** it can report `RateLimited`, and a caller can tell that failure apart from `BufferFull`

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
