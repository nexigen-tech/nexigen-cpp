# Spec Delta

## MODIFIED Requirements

### Requirement: CPU core affinity

`MetricProviderConfig` SHALL carry a `ThreadSpec` for the existing harvest worker instead of a uint64 core mask. When harvesting is enabled, the worker SHALL apply the spec to itself before delivering any batch, supporting CPUs above 63, naming, and Other/FIFO/RR scheduling. An empty CPU set SHALL leave affinity unchanged. Best-effort failures SHALL be reported without stopping collection; a `Required` failure SHALL prevent the harvest loop and publish a failed `startup_status()` without leaking a worker. A zero interval SHALL create no worker and apply no attributes.

#### Scenario: Harvester affinity is applied

- **WHEN** the provider starts with valid worker attributes and the test queries them on the worker
- **THEN** the name, affinity, policy, and priority are applied and every background sink callback runs on that worker

#### Scenario: Invalid mask is visible

- **WHEN** a requested affinity or scheduler operation fails under `BestEffort`
- **THEN** `startup_status()`/the thread report exposes the failure and batches continue with any successfully applied attributes

#### Scenario: Required failure prevents harvesting

- **WHEN** a requested attribute fails under `Required`
- **THEN** no harvest loop runs, `startup_status()` is an error, and provider destruction is safe without a live worker

#### Scenario: CPU above 63 is supported

- **WHEN** the worker spec contains a permitted CPU above 63
- **THEN** the harvester uses it without a uint64 mask conversion

#### Scenario: Zero mask leaves affinity alone

- **WHEN** the worker CPU set is empty
- **THEN** the worker inherits the process affinity and no pinning call is required to succeed

#### Scenario: Disabled harvesting applies nothing

- **WHEN** `collect_interval` is zero
- **THEN** no worker is created and no thread-attribute system call is made

### Requirement: Provider configuration serialization

`MetricProvider_config` SHALL implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` for its complete visible state under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization, including the complete worker thread spec.

#### Scenario: Configuration JSON round-trip

- **WHEN** a `MetricProvider_config` with a 10 ms interval and a named worker on CPUs `{2, 70}` with FIFO priority 30 is serialized and parsed back
- **THEN** the JSON carries the interval in nanoseconds and every worker attribute, and the parsed configuration compares equal to the original

#### Scenario: Configuration renders diagnostics

- **WHEN** `to_string()` is called on a configuration and the free `to_string` is called on the same object
- **THEN** both return equal, non-empty strings describing the interval and worker spec

#### Scenario: Configuration formats

- **WHEN** `std::format("{}", config)` is evaluated
- **THEN** the result equals the configuration's `to_string()` text
