# logging/multi-producer Specification

## Purpose
Defines the wait-free multi-producer transport shared by all asynchronous nexigen loggers: a pre-allocated array of per-thread SPSC ring shards, `thread_local` slot registration, sharded drop accounting, and a background consumer that drains every active shard without ever blocking a producer.

## Requirements

### Requirement: Pre-allocated per-thread shard array

Each asynchronous logger SHALL own a fixed compile-time-sized array of `SpscRingBuffer` shards (`MaxThreads`, default 32, overridable per instance), each shard carrying `LogRecord`s with its own power-of-two `ShardCapacity`. All shard storage SHALL exist before the first log call on any thread; steady-state operation SHALL allocate nothing.

#### Scenario: Shards are static storage

- **WHEN** a logger is constructed and initialized
- **THEN** all `MaxThreads` ring arrays are resident (verified by a steady-state allocation counter showing zero growth across multi-thread logging)

#### Scenario: Each shard keeps SPSC wait-free layout

- **WHEN** shard internals are inspected in static assertions
- **THEN** every shard is a distinct cache-line-aligned SPSC ring with its own producer and consumer index cells (no shared write index across shards)

### Requirement: Thread-local slot registration

On a thread's first log call against a logger instance, the logger SHALL assign that thread an exclusive shard slot — one bounded atomic fetch-add on a registration counter plus a `thread_local` write — and every later call on that thread SHALL route from the thread-local slot index with no atomic touching any shared index.

#### Scenario: Two threads get two distinct shards

- **WHEN** two producer threads log to the same logger instance
- **THEN** each thread's records land only in its own shard, and a third thread observing the shards sees no interleaved producer inside one shard

#### Scenario: Steady-state path never contends

- **WHEN** N producer threads log accepted records in a measured loop after registration
- **THEN** no call performs a CAS loop, atomic RMW on a shared write index, mutex, spin, yield, or sleep, and TSan reports no data races

#### Scenario: Registration happens once per thread

- **WHEN** one thread issues a million log calls
- **THEN** exactly one slot registration occurs for that thread on that logger and the remaining calls read only thread-local state

### Requirement: Sharded drop accounting without contention

Ring-full drops SHALL be counted per shard by its own producer (no cross-thread counter on the accepted path) and aggregated by `dropped()` snapshots; over-throttle drops SHALL likewise be per-shard. The aggregate drop story (rate-limited vs buffer-full separation, drop summary lines) is unchanged.

#### Scenario: Full shard drops only that shard's producer

- **WHEN** one thread's shard is full while other threads' shards have room
- **THEN** only the full-shard producer's records are dropped and counted, and the other threads continue accepting

#### Scenario: Snapshot aggregates all shards

- **WHEN** `dropped()` is called from any thread after drops on several shards
- **THEN** the returned statistics are the sum over all shards for each drop cause (a snapshot, not synchronized linearization)

### Requirement: Consumer drains every active shard

The background worker SHALL continuously round-robin all registered shards, popping bounded batches from each until every shard is momentarily empty, then applying its idle backoff; no shard may be starved indefinitely and producers are never aware of draining.

#### Scenario: All threads' records reach the sink

- **WHEN** 4–8 threads each log a known distinct record count and the logger is flushed
- **THEN** the sink observes every record exactly once, per-shard order FIFO, and the flush barrier completes

#### Scenario: One busy shard does not stall another

- **WHEN** one producer floods its shard to capacity while another logs occasionally
- **THEN** the occasional producer's calls keep succeeding between consumer batches, bounded only by its own shard's capacity

### Requirement: Slot exhaustion drops instead of blocking

When a `MaxThreads`-th distinct producer thread is followed by additional threads, calls from the extra threads SHALL be dropped and counted in a dedicated slot-exhausted total exposed through `dropped()`, never routed into another thread's shard, never blocking, and never corrupting an existing shard's SPSC contract.

#### Scenario: Thread beyond capacity loses records safely

- **WHEN** `MaxThreads + 1` distinct threads each log once to the same logger
- **THEN** all registrations up to `MaxThreads` proceed normally, the final thread's call returns immediately having enqueued nothing, and its record is reflected in the slot-exhausted count
