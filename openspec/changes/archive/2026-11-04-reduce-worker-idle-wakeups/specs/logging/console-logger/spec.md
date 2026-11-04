# Spec Delta

## Purpose

The console writer's idle behaviour moves to the shared `core::WorkerTuning` policy, and the poll ceiling becomes configurable through `ConsoleLoggerConfig`.

## ADDED Requirements

### Requirement: Background writer uses the shared idle-backoff policy

`ConsoleLogger`'s background writer SHALL poll its shard rings, and when idle SHALL apply `core::WorkerTuning`: a short spin budget, then a sleep that doubles from 100 µs up to the configured ceiling, with its timer slack widened to half the ceiling. Producers SHALL never wait on this, and `flush()` still completes within the poll ceiling.

#### Scenario: Idle console writer wake-ups are bounded by the ceiling

- **WHEN** a `ConsoleLogger` runs idle with the default ceiling
- **THEN** its writer thread performs on the order of one wake-up per ceiling interval (≈1 ms), not one per 200 µs

### Requirement: Console logger config exposes the poll ceiling

`ConsoleLoggerConfig` SHALL carry `worker_poll_interval_us` (default `1000`) naming the ceiling of the writer's idle sleep, clamped to `[50, 100000]` by the worker.

#### Scenario: Config dump renders the ceiling

- **WHEN** `to_string()` is called on a `ConsoleLoggerConfig` with a non-default `worker_poll_interval_us`
- **THEN** the string contains the `worker_poll_interval_us` field and its value

## MODIFIED Requirements

### Requirement: Console logger diagnostic strings

`ConsoleLogger::Config` and `ConsoleLogger` SHALL each provide a cold-path `std::string to_string() const` (plus the matching free `to_string(const T&)`): the Config dump names its initial level and worker poll interval; the logger dump reports level, shard capacity, aggregated pending records, and drop totals as a third-thread snapshot.

#### Scenario: Config and logger render

- **WHEN** `to_string()` is called on a `ConsoleLogger::Config` at `Debug` and on its running logger
- **THEN** the Config string names `debug` (and any non-default poll interval), and the logger string names the level, capacity, pending, and dropped counters
