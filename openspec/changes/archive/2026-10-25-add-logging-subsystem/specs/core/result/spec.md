# Spec Delta

## MODIFIED Requirements

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
