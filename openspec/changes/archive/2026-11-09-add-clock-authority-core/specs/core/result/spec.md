# Spec Delta

## MODIFIED Requirements

### Requirement: Error code catalogue

The system SHALL expose a fixed set of error codes in `nexigen::core`: `Ok`, `BufferFull`, `BufferEmpty`, `InvalidArgument`, `SystemError`, `NotInitialized`, `RateLimited`, `Conflict`, and `ResourceExhausted`. Each code SHALL be a distinct, addressable enumerator usable as a non-throwing failure value. `RateLimited` classifies a rate-policy rejection before queue interaction; `Conflict` classifies an operation rejected because another owner holds a process-global resource or state (distinct from `NotInitialized`); `ResourceExhausted` classifies a request rejected because the whole fixed-capacity pool of a preallocated resource is already issued (distinct from `BufferFull`'s bounded-queue push rejection).

#### Scenario: Enumerators are available and distinct

- **WHEN** a caller names each of the nine error codes and compares any two different codes
- **THEN** all nine are usable as error values and no two compare equal

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

#### Scenario: Resource pool exhaustion is distinguishable from queue rejection

- **WHEN** stamp-source issuance fails after every source of the fixed bank is issued while the codebase queues stay untouched
- **THEN** the call reports `ResourceExhausted` and a caller can tell the pool grant failure apart from `BufferFull`

#### Scenario: ResourceExhausted has stable value and naming

- **WHEN** `ErrorCode::ResourceExhausted` is converted to its underlying value, `to_string`, and JSON
- **THEN** the value is 8, the name is `"ResourceExhausted"`, and `from_json`/`from_string` recover the same enumerator under `JSON_NOEXCEPTION=1`, with `std::format` rendering `"ResourceExhausted"`

### Requirement: Compile-time error code naming

The system SHALL provide a `to_string` mapping from an error code to its human-readable name that is evaluated at compile time (`constexpr`) and returns a string view referring to static storage, with no allocation and no exception path.

#### Scenario: Name of a known code is resolvable at compile time

- **WHEN** `to_string` is applied to each of the nine defined error codes
- **THEN** each call yields that code's exact name (for example `BufferFull` yields the text `BufferFull`, `RateLimited` yields the text `RateLimited`, and `ResourceExhausted` yields the text `ResourceExhausted`), and the calls are valid in a constant expression

#### Scenario: Naming allocates nothing

- **WHEN** `to_string` is called on the hot path
- **THEN** no memory is allocated and no dynamic lifetime is created or destroyed
