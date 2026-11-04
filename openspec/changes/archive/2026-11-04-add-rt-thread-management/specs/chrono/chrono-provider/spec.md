# Spec Delta

## MODIFIED Requirements

### Requirement: CPU core affinity

`ChronoProviderConfig` SHALL carry a `ThreadSpec` for the existing snapshot worker instead of a uint64 core mask. When `start()` launches a worker, it SHALL apply the spec to itself before delivering any snapshot, supporting CPUs above 63, naming, and Other/FIFO/RR scheduling. An empty CPU set SHALL leave affinity unchanged. Best-effort failures SHALL remain visible without stopping delivery; a `Required` failure SHALL make `start()` fail before the snapshot loop and leave no running worker. A zero interval SHALL retain deferred/no-worker behavior.

#### Scenario: Worker affinity is applied

- **WHEN** the provider starts with valid worker attributes
- **THEN** the worker's name, affinity, policy, and priority are applied before its first snapshot

#### Scenario: Invalid mask is visible without stopping delivery

- **WHEN** an affinity or scheduler request fails under `BestEffort`
- **THEN** `startup_status()`/the thread report exposes the failure, the worker still runs with any applied attributes, and snapshots flow

#### Scenario: Required failure prevents snapshot delivery

- **WHEN** an attribute request fails under `Required`
- **THEN** `start()` returns `SystemError`, no snapshot loop runs, the worker is joined, and provider destruction is safe

#### Scenario: CPU above 63 is supported

- **WHEN** the worker spec contains a permitted CPU above 63
- **THEN** the snapshot worker uses it without truncating to a uint64 mask

#### Scenario: Zero mask leaves affinity alone

- **WHEN** the worker CPU set is empty
- **THEN** the worker inherits process affinity and no pinning call is required to succeed

#### Scenario: Zero interval applies no attributes

- **WHEN** `collect_interval` is zero and `start()` is called
- **THEN** no worker is created and no thread-attribute operation occurs

### Requirement: Provider configuration serialization

`ChronoProvider_config` SHALL implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` for its complete visible state under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization, including the complete worker thread spec.

#### Scenario: Configuration JSON round-trip

- **WHEN** a `ChronoProvider_config` with an interval and a named FIFO worker on a CPU above 63 is serialized and parsed back
- **THEN** the JSON carries the interval in nanoseconds and every worker attribute, and the parsed configuration compares equal to the original

#### Scenario: Configuration renders and formats

- **WHEN** `to_string()`, the free `to_string`, and `std::format("{}", config)` are applied
- **THEN** the member and free results are equal non-empty strings and the formatter renders the same text
