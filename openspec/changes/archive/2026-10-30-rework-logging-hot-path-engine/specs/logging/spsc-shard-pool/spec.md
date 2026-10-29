# Spec Delta

## Purpose

Defines `SpscShardPool`: a pre-allocated pool of per-thread SPSC shard rings with runtime sizing, `thread_local` slot registration, `Draining` shard lifecycle after thread exit, and pool-return of free shards.

## ADDED Requirements

### Requirement: Pre-allocated shard pool

The system SHALL allocate all memory for `max_producer_threads` shard rings (each with `spsc_ring_buffer_capacity` slots) exactly once during logger initialization; steady-state logging performs zero heap allocations.

#### Scenario: Zero steady-state allocation

- **WHEN** a measured window of accepted log calls runs after initialization
- **THEN** process heap-usage counters show zero change across the window

### Requirement: Thread-local slot registration and pool exhaustion

On a thread's first log call against a logger instance, the logger SHALL assign that thread an exclusive shard slot via one bounded atomic `fetch_add` on a registration counter plus a `thread_local` write. When `max_producer_threads` distinct threads are already registered, additional threads SHALL increment `unregistered_thread_drops_` and drop their records without blocking, allocating, or aborting.

#### Scenario: Pool exhaustion counts drops instead of aborting

- **WHEN** `max_producer_threads + 1` distinct threads each log once to the same logger
- **THEN** the first `max_producer_threads` threads register normally, the final thread's call returns immediately having enqueued nothing, and `unregistered_thread_drops_` is incremented

### Requirement: Shard Draining and pool return

When a registered producer thread exits, its `thread_local` registration destructor SHALL transition the shard to `Draining`; after the consumer drains the shard, it SHALL return to the free pool for reuse by a later thread.

#### Scenario: Freed shard is reusable

- **WHEN** a producer thread exits after logging, and a new thread subsequently logs
- **THEN** the new thread may be assigned the exited thread's former shard slot, and no record is lost or duplicated
