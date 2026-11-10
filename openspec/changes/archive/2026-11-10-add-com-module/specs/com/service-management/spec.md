# Spec Delta

## Purpose

Defines the module foundation: the `com::Error` vocabulary with the full project conversion set, `ServiceName` and `Node` daemon-less service discovery, the protocol-independent API boundary (business code never names `iox2::`), background-thread CPU isolation, and the minimal service manifest.

## ADDED Requirements

### Requirement: Error vocabulary

`nexigen::com` SHALL expose `com::Error` as its own enum class with the enumerators `Ok`, `ServiceAlreadyExists`, `ServiceNotFound`, `CreationFailed`, `ConnectionFailure`, `IncompatibleTypeLayout`, `OutOfSamples`, `UnableToDeliver`, `Timeout`, `InvalidArgument`, and `Unknown`, and SHALL define `com::Result<T>` and `com::Status` as `std::expected` over it with the project's never-`value()` rule.

#### Scenario: Error codes are distinct and round-trip

- **WHEN** each `com::Error` value passes through `to_string`, `from_string`, `to_json`, and `from_json` under `JSON_NOEXCEPTION=1`
- **THEN** every canonical name round-trips, an out-of-range cast renders `Unknown`, a hostile JSON value leaves the target unchanged, and `std::format("{}", error)` renders the canonical name

#### Scenario: Error is separate from core vocabulary

- **WHEN** a translation unit names both `nexigen::core::ErrorCode` and `nexigen::com::Error`
- **THEN** the two enums are distinct types and a com operation reports its failures as `com::Error` only

### Requirement: Service naming

`com::ServiceName` SHALL be a value type holding the service identity, constructible from a non-empty string, with `to_string()` and equality; port creation SHALL require a valid `ServiceName`.

#### Scenario: Service name is a value

- **WHEN** a `ServiceName` is copied and compared to the original
- **THEN** copies compare equal, `to_string()` returns the name, and the formatter renders it

### Requirement: Node and daemon-less discovery

`com::Node` SHALL own one process's membership in the communication fabric, and service discovery SHALL be decentralized: a publisher may start before any subscriber and a subscriber may wait for a publisher that appears later, with no central broker or PID binding.

#### Scenario: Late subscriber joins an existing service

- **WHEN** a publisher is already sending and a subscriber for the same `ServiceName` connects
- **THEN** the subscriber receives subsequent samples without restarting the publisher

#### Scenario: Subscriber waits for a future publisher

- **WHEN** a subscriber is created before any publisher exists and a publisher starts later
- **THEN** the subscriber connects and receives samples once the publisher appears

### Requirement: Protocol-independent boundary

The public surface SHALL expose only `nexigen::com` types: business code SHALL create `Publisher`/`Subscriber`/`Client`/`Server`/`Field` through the com service interface and SHALL never name `iox2::` types, so a future transport swap changes the module, not the applications.

#### Scenario: Business code is transport-agnostic

- **WHEN** an example application publishes and receives a message using only `nexigen::com` names
- **THEN** no `iox2::` symbol appears in the application translation unit

### Requirement: Background-thread CPU isolation

Any background thread the module owns (telemetry aggregation, future gateways) SHALL read the background CPU mask from the process environment and pin itself to those service cores, never to real-time cores.

#### Scenario: Infrastructure threads pin to background cores

- **WHEN** the background mask is set and a com background thread starts
- **THEN** the thread's affinity equals the configured background mask

### Requirement: Minimal service manifest

The module SHALL provide a `ServiceManifest` value type listing `provided_services` and `required_services` (name, `max_subscribers`, `history_size`, `buffer_capacity`, `report_overflow_to_phm`), serializable to JSON and parseable back without throwing under `JSON_NOEXCEPTION=1`.

#### Scenario: Manifest round-trips

- **WHEN** a `ServiceManifest` with one provided and one required service is serialized and parsed back
- **THEN** the parsed manifest equals the original, `to_string()` and `std::format("{}", manifest)` render it, and a hostile JSON input leaves the target unchanged
