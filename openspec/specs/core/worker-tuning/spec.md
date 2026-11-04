# core/worker-tuning Specification

## Purpose

Defines the shared idle-backoff policy for background consumers (the logging `detail::AsyncWorker` and the tracing `TracerProvider` consumer): a bounded spin budget before sleeping, a geometric sleep that grows to a poll ceiling, and a widened timer slack so idle workers pinned to a shared core coalesce their wake-ups instead of each burning the core with context switches.

## Requirements

### Requirement: WorkerTuning defines the idle-backoff policy

`nexigen::core::WorkerTuning` SHALL be a header-only struct carrying `poll_interval_us` (default `1000`, the ceiling of the idle sleep) and `spin_budget` (default `4`, the number of failed sweeps before the first sleep), plus these policy constants: `default_poll_interval_us = 1000`, `min_poll_interval_us = 50`, `max_poll_interval_us = 100000`, `default_spin_budget = 4`, and `backoff_floor_us = 100`.

#### Scenario: Defaults match the documented policy

- **WHEN** a `WorkerTuning` is default-constructed
- **THEN** `poll_interval_us` is `1000` and `spin_budget` is `4`

### Requirement: Poll ceiling is clamped to a supported range

`WorkerTuning::clamp_poll_us(requested)` SHALL return `requested` when it lies within `[min_poll_interval_us, max_poll_interval_us]`, and otherwise the nearest bound.

#### Scenario: Requests outside the range are clamped

- **WHEN** `clamp_poll_us(0)` and `clamp_poll_us(1000000)` are evaluated
- **THEN** the results are `50` and `100000` respectively, and `clamp_poll_us(1000)` is `1000`

### Requirement: Backoff sleeps grow geometrically to the ceiling

`WorkerTuning::backoff_us(failed_sleeps, poll_max_us)` SHALL return the sleep length for the next idle sleep: `backoff_floor_us` for the first sleep, doubling each subsequent sleep, capped at `poll_max_us` (and never below it when the ceiling is itself below the floor).

#### Scenario: Sleeps double up to the ceiling

- **WHEN** `backoff_us` is evaluated for consecutive `failed_sleeps` values 1, 2, 3 with `poll_max_us = 1000`
- **THEN** the results are `100`, `200`, `400`, and further steps keep doubling until they are clamped to `1000`

### Requirement: Timer slack is widened on the worker thread

`WorkerTuning::apply_timer_slack(poll_max_us)` SHALL, when called on the worker thread, raise that thread's timer slack to `poll_max_us / 2` nanoseconds via `prctl(PR_SET_TIMERSLACK)`, never below the kernel default (50 µs), so idle sleeps of co-located consumers coalesce; a failure SHALL be harmless (the default slack stays).

#### Scenario: Slack never drops below the kernel default

- **WHEN** `apply_timer_slack(1000)` runs on a worker thread
- **THEN** the thread's timer slack becomes 500 µs (half the ceiling), and `apply_timer_slack(50)` does not narrow it below 50 µs
