# Spec Delta

## Purpose

The background writer's idle behaviour moves from the hard-coded 64-sweep spin + 200 µs sleep to the shared `core::WorkerTuning` policy, and the poll ceiling becomes configurable through `FileLoggerConfig`.

## ADDED Requirements

### Requirement: Background writer uses the shared idle-backoff policy

`FileLogger`'s background writer SHALL poll its shard rings, and when idle SHALL apply `core::WorkerTuning`: a short spin budget, then a sleep that doubles from 100 µs up to the configured ceiling (never waking more often than the ceiling), with its timer slack widened to half the ceiling so idle writers pinned to a shared core coalesce. Producers SHALL never wait on this; the first record after idle is drained within the backoff floor, and `flush()` still completes within the poll ceiling.

#### Scenario: Idle writer wake-ups are bounded by the ceiling

- **WHEN** a `FileLogger` runs idle with the default ceiling
- **THEN** its writer thread performs on the order of one wake-up per ceiling interval (≈1 ms), not one per 200 µs

#### Scenario: A raised ceiling reduces wake-ups further

- **WHEN** the ceiling is configured to 20 ms and the logger runs idle
- **THEN** the writer's wake-up rate drops proportionally, and an enqueued record is still flushed within the ceiling

### Requirement: File logger config exposes the poll ceiling

`FileLoggerConfig` SHALL carry `worker_poll_interval_us` (default `1000`) naming the ceiling of the writer's idle sleep, clamped to `[50, 100000]` by the worker.

#### Scenario: Config dump renders the ceiling

- **WHEN** `to_string()` is called on a `FileLoggerConfig` with a non-default `worker_poll_interval_us`
- **THEN** the string contains the `worker_poll_interval_us` field and its value

## MODIFIED Requirements

### Requirement: File logger vocabulary diagnostic strings

`Throttle_config`, `Rotation_config`, `FileLogger_config`, and `Dropped_stats` SHALL each provide a cold-path `std::string to_string() const` (plus matching free `to_string(const T&)`) rendering every field (enabled flag, budgets and windows, schedule and custom period, app/base_dir/file_name/level/max_file_size_bytes/core_mask/worker_poll_interval_us, per-cause drop totals) for configuration dumps and test evidence.

#### Scenario: Config dump is complete

- **WHEN** `to_string()` is called on a populated `FileLogger_config`
- **THEN** the returned string contains the app name, base directory, file name, level name, maximum file size, throttle window, rotation schedule name, core mask, and the worker poll interval

#### Scenario: Drop snapshot renders

- **WHEN** `to_string()` is called on a `Dropped_stats` with nonzero counters
- **THEN** the string names each cause and count
