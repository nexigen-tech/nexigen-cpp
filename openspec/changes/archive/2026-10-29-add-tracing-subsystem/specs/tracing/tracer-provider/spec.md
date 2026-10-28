# Spec Delta

## Purpose

Defines the TracerProvider → Tracer → ScopedSpan factory hierarchy: the pre-allocated per-thread SPSC shard pool, the CPU-pinned background consumer, runtime enable and sampling control, drop visibility, and shutdown drain.

## ADDED Requirements

### Requirement: Provider and tracer factory hierarchy

`TracerProvider` SHALL be the only source of `Tracer` instances: `get_tracer(scope_name)` returns an existing tracer for a repeated scope name and a distinct tracer for a new name; tracers are created from a pre-allocated pool sized by configuration, with pool exhaustion returning `BufferFull`; tracer and span construction outside the factory hierarchy SHALL fail at compile time; handles stay valid until provider destruction.

#### Scenario: Repeated scope names return the same tracer

- **WHEN** `get_tracer("engine")` is called twice on one provider
- **THEN** both calls return the same tracer, whose diagnostic `to_string()` renders the scope name

#### Scenario: Distinct scope names return distinct tracers

- **WHEN** `get_tracer` is called with two different names
- **THEN** two distinct tracers are returned, each rendering its own scope name, and spans start through both concurrently

#### Scenario: Pool exhaustion is surfaced

- **WHEN** more distinct scope names are requested than the configured tracer pool holds
- **THEN** the excess request returns `BufferFull` and previously created tracers remain usable

#### Scenario: Factory-only construction is pinned at compile time

- **WHEN** a translation unit attempts to construct a `Tracer` or a `ScopedSpan` directly
- **THEN** compilation fails — only the provider hierarchy can create them

### Requirement: Pre-allocated provider memory

`TracerProvider` SHALL pre-allocate its entire memory footprint at construction as one aligned block — a pool of up to `Max_threads` (32) per-thread `SpscRingBuffer` shards, each of `ShardCapacity` records, plus the per-shard accounting and tracer pool — so span recording, shard drain, and sink delivery never allocate.

#### Scenario: Recording runs allocation-free

- **WHEN** a provider is constructed and spans are started, updated, and ended across threads with the allocation counter armed
- **THEN** zero heap allocations occur after construction returns

### Requirement: Per-thread shard ownership

Each producer thread SHALL own exactly one exclusive shard acquired through `thread_local` slot registration with no lock and no CAS on the steady path; slots are never recycled, and threads beyond the `Max_threads` pool are dropped and counted rather than blocked.

#### Scenario: Up to 32 producer threads get exclusive shards

- **WHEN** 32 distinct threads each start and end spans on one provider
- **THEN** every span drains exactly once, and no thread's shard interleaves with another's slot storage

#### Scenario: Threads beyond the pool drop visibly

- **WHEN** a 33rd distinct thread starts spans on a pool whose 32 slots are claimed
- **THEN** those spans drop without blocking, `slot_exhausted` counts them, and the first 32 threads are unaffected

### Requirement: CPU-pinned background consumer

`TracerProvider` SHALL run one background consumer worker (`std::jthread`) that self-pins to the configured `core_mask` via `pthread_setaffinity_np` before its first delivery (a zero mask inherits OS placement), drains all registered shards round-robin with a bounded per-sweep slice so one flooded shard cannot starve the others, and delivers each record to the bound sink — while producers never wait on the worker; idle periods use bounded backoff so a pinned core is not burned busy-polling.

#### Scenario: Worker affinity matches the configured mask

- **WHEN** a provider is created with a valid `core_mask` and the worker's actual affinity is read back with `pthread_getaffinity_np`
- **THEN** the worker's mask equals the configured mask, and it is reported by the provider's startup snapshot

#### Scenario: Pin failures are visible without stopping the pipeline

- **WHEN** a provider is created with an invalid mask (a bit beyond the machine's cores)
- **THEN** the startup snapshot reports `SystemError`, the worker still runs unpinned, and spans still record and drain

#### Scenario: Per-shard FIFO and fair drain hold

- **WHEN** multiple producer threads record spans concurrently and the sink drains them
- **THEN** each thread's spans drain in that thread's start order, and no shard is starved by a flooded sibling

#### Scenario: Producers never wait on the worker

- **WHEN** the worker is momentarily delayed and producers keep ending spans
- **THEN** every producer operation completes in bounded time — delays surface only as counted drops, never as blocking

### Requirement: Dynamic runtime control

`TracerProvider` SHALL support runtime enable/disable and sampling decision control through relaxed atomics read once per span start: a disabled provider or a `Drop` decision short-circuits at approximately 1 nanosecond, and a control change made by any thread is visible to subsequent spans on every thread without locks or ordering handshakes.

#### Scenario: Disable and re-enable mid-run

- **WHEN** a provider records spans, is disabled mid-run, records more spans, and is re-enabled
- **THEN** only the spans started while enabled drain, and the spans started after re-enable drain normally

#### Scenario: Sampling control is immediate

- **WHEN** the sampling decision is switched from `RecordAndSample` to `Drop` and back between span starts
- **THEN** exactly the spans started under `RecordAndSample` drain

#### Scenario: Control reads need no synchronization

- **WHEN** one thread repeatedly flips control while another repeatedly starts spans
- **THEN** no mutex, condition variable, or CAS loop participates in span start, and every drained record reflects the decision that was active at its start

### Requirement: Drop visibility

The provider SHALL expose a `dropped()` snapshot counting each drop cause separately — `buffer_full` (a caller's shard was full), `rate_limited` (the configured span-rate budget rejected the span), `slot_exhausted` (no free shard slot) — readable at any time from any thread as a relaxed aggregate observation, never as synchronization; sampling-filtered spans are not counted as drops.

#### Scenario: Full shards count as buffer_full

- **WHEN** spans end into a shard that is full while the consumer is stalled
- **THEN** each rejected push increments `buffer_full` and nothing else, and the count equals the number of rejected spans

#### Scenario: The span-rate budget counts rate_limited

- **WHEN** a provider has a per-window span budget configured and producers exceed it within one window
- **THEN** the excess spans are rejected before queue interaction and `rate_limited` counts them exactly

#### Scenario: Sampling is not a drop

- **WHEN** spans are short-circuited by disable or `Drop` sampling
- **THEN** all three drop counters remain at zero

### Requirement: Shutdown drain and flush barrier

`TracerProvider` destruction SHALL stop the worker, drain every accepted record to the sink, and leave no record accepted before destruction undelivered; a `flush()` control-plane barrier SHALL return only after every span accepted before the call is sink-durable; operations on handles after provider destruction SHALL fail with `NotInitialized` rather than corrupt state.

#### Scenario: Destruction does not lose accepted records

- **WHEN** producers end spans and the provider is destroyed immediately after
- **THEN** every span accepted before destruction is delivered to the sink before the destructor returns

#### Scenario: Flush waits for durability

- **WHEN** spans are ended and `flush()` is called
- **THEN** the call returns only after those spans have drained and the sink's flush has completed, surfacing sink errors

#### Scenario: Dead handles report NotInitialized

- **WHEN** a tracer handle outlives its provider and starts a span
- **THEN** the operation fails with `NotInitialized` and no memory error or sanitizer diagnostic occurs

### Requirement: Provider serialization surfaces

`TracerProvider`'s configuration SHALL provide member/free `to_string`, full `to_json`/`from_json` round-trip, and `std::formatter`; the provider, its `Tracer` handles, and its drop snapshot SHALL provide member/free `to_string` and `std::formatter`, plus `to_json` with deliberately no `from_json` since they are factory-bound live objects.

#### Scenario: Provider config round-trips through JSON

- **WHEN** a configuration (core mask, tracer pool size, span-rate budget) is serialized and parsed back
- **THEN** every field survives the round-trip under `JSON_NOEXCEPTION=1`, and `std::format` renders the `to_string()` text

#### Scenario: Handles and snapshots render diagnostics

- **WHEN** `to_string()` is evaluated on the provider, a tracer, and a drop snapshot
- **THEN** the provider renders its configuration and state, the tracer renders its scope name, the snapshot renders per-cause counts, and in each case `std::format("{}", value)` equals the member's text

#### Scenario: Live provider objects are not deserializable

- **WHEN** a translation unit attempts `from_json` into a `TracerProvider`, `Tracer`, or the drop snapshot type where none exists
- **THEN** compilation fails for the provider and tracer (no overload exists), while the drop snapshot — a value type — parses successfully
