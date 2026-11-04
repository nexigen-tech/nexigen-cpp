# Spec Delta

## ADDED Requirements

### Requirement: Console writer accepts a thread specification

`ConsoleLoggerConfig` SHALL carry an optional `ThreadSpec` for its existing writer. The writer SHALL apply it before the drain loop without changing stream routing, backoff, or flush behavior. An omitted/default spec SHALL preserve inherited affinity and scheduling; legacy file-logger affinity settings SHALL NOT implicitly pin the console writer.

#### Scenario: Console writer applies valid attributes

- **WHEN** a console config supplies a name, CPUs, and FIFO/RR attributes
- **THEN** their application follows the common required/best-effort contract before the first console line

#### Scenario: Default console behavior is unchanged

- **WHEN** no worker thread spec is supplied
- **THEN** the console writer starts with inherited attributes and performs no new steady-state work

#### Scenario: Required failure prevents console output

- **WHEN** a required attribute cannot be applied
- **THEN** console initialization fails and its writer does not enter the output loop

## MODIFIED Requirements

### Requirement: Console logger diagnostic strings

`ConsoleLogger::Config` and `ConsoleLogger` SHALL each provide a cold-path `std::string to_string() const` (plus the matching free `to_string(const T&)`): the Config dump names its initial level, worker thread spec, and worker poll interval; the logger dump reports level, shard capacity, aggregated pending records, and drop totals as a third-thread snapshot. The config SHALL provide non-throwing JSON round-trip and formatter support for the thread spec.

#### Scenario: Config and logger render

- **WHEN** `to_string()` is called on a `ConsoleLogger::Config` at `Debug` and on its running logger
- **THEN** the Config string names `debug`, the worker spec, and any non-default poll interval, and the logger string names the level, capacity, pending, and dropped counters

#### Scenario: Config JSON and formatter preserve the worker spec

- **WHEN** a config with a named worker on a CPU above 63 is serialized and parsed under `JSON_NOEXCEPTION=1`
- **THEN** the worker spec round-trips and `std::format("{}", config)` equals the config's `to_string()`
