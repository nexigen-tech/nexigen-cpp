# logging/log-level Specification

## Purpose

Defines the severity levels a `nexigen` logger recognizes, how a logger's active threshold is read and changed safely during execution, and the bounded-cost short-circuit that keeps calls below the threshold out of every other hot-path stage.

## Requirements

### Requirement: Runtime level control

Every logger SHALL expose `set_level(nexigen::core::LogLevel) noexcept` and `level() const noexcept -> nexigen::core::LogLevel`, and SHALL honor a level change made at any point during execution: a relaxed atomic load of the active threshold on each log call determines filtering, and a concurrent `set_level` is visible to subsequent calls without locks or ordering handshakes.

#### Scenario: Level raised mid-run filters more

- **WHEN** a logger running at `Debug` is set to `Error` and then called with `Info`
- **THEN** the `Info` call is filtered and does not reach the output sink

#### Scenario: Level lowered mid-run admits more

- **WHEN** a logger at `Off` is set to `Verbose` and then called with `Verbose`
- **THEN** the `Verbose` call is accepted and reaches the output sink

#### Scenario: Reads and writes need no synchronization primitives

- **WHEN** one thread repeatedly reads `level()` while another repeatedly calls `set_level`
- **THEN** no mutex, condition variable, or CAS loop is used, and every observed value is one the other thread actually stored

### Requirement: Sub-threshold calls short-circuit

A log call whose level is below the active threshold, or any call while the threshold is `Off`, SHALL return control to the application within a hard ceiling of 2 ns (or 10 CPU cycles) on the target hardware in Release. The path performs at most one relaxed atomic load, one comparison, and an immediate return, touching no shard ring buffer, timestamp source, or payload; the steady-state median is approximately 1 ns.

#### Scenario: Filtered call performs no logging work

- **WHEN** a call below the threshold is issued while the calling thread's shard ring is completely full
- **THEN** the call still returns immediately and enqueues nothing (the filtered path never probes the ring)

#### Scenario: Filtered call reports success

- **WHEN** a call is filtered by the level check
- **THEN** it returns normally with no drop counted — filtering is normal operation, not a failure

#### Scenario: Filtered-path latency is bounded

- **WHEN** a logging method is called with a level below the active threshold (for example, a debug call while `Warn` is active) in a measured loop in a Release build
- **THEN** its median latency is approximately 1 ns and its p99.9 does not exceed 2 ns (or 10 CPU cycles), with the maximum reported for visibility
