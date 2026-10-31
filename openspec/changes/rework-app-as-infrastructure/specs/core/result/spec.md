# Spec Delta

## MODIFIED Requirements

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
