# logging/multi-producer delta spec

## MODIFIED Requirements

### Requirement: Thread-local slot registration

On a thread's first log call against a logger instance, the logger SHALL assign
that thread an exclusive shard slot — one bounded atomic fetch-add on a
registration counter plus a `thread_local` write — and every later call on that
thread SHALL route from the thread-local slot index with no atomic touching any
shared index. The per-thread registration cache SHALL hold at least 8 distinct
pools per thread; when a thread logs to more cached pools than the cache holds,
the excess pools SHALL NOT register per call (no global slot burn) — their
records SHALL be dropped and accounted through the existing
`slot_exhausted` cell exactly like pool exhaustion, without blocking or
allocating.

#### Scenario: Two threads get two distinct shards

- **WHEN** two producer threads log to the same logger instance
- **THEN** each thread's records land only in its own shard, and a third thread
  observing the shards sees no interleaved producer inside one shard

#### Scenario: Steady-state path never contends

- **WHEN** N producer threads log accepted records in a measured loop after
  registration
- **THEN** no call performs a CAS loop, atomic RMW on a shared write index,
  mutex, spin, yield, or sleep, and TSan reports no data races

#### Scenario: TLS cache exhaustion drops without burning registration slots

- **WHEN** a thread emits to more distinct pools than its per-thread cache
  holds (e.g. a ninth pool on a thread still using eight earlier pools)
- **THEN** those pool's edges take the counted `slot_exhausted` drop path, the
  global registration counter does NOT advance on every record, and previously
  cached pools on that thread keep their wait-free steady path

#### Scenario: Registration happens once per thread

- **WHEN** one thread issues a million log calls
- **THEN** exactly one slot registration occurs for that thread on that logger
  and the remaining calls read only thread-local state
