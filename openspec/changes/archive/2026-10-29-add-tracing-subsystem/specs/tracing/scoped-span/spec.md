# Spec Delta

## Purpose

Defines the RAII span lifecycle: wait-free attribute and status updates, the sub-20-nanosecond enqueue into the calling thread's private shard, the sampling and enable short-circuit, drop visibility, and cross-thread context linking.

## ADDED Requirements

### Requirement: RAII span lifecycle

`ScopedSpan` SHALL be a non-copyable RAII guard created exclusively by a `Tracer`'s `start_span(name, kind, parent_context, source_location)`: it records the start timestamp on construction, and on destruction — or on an explicit `end()` — pushes exactly one span record into the calling thread's private shard; an explicit `end()` followed by destruction SHALL NOT push a second record.

#### Scenario: Construction stamps the start

- **WHEN** a span is created, held while time advances, and ended
- **THEN** the drained record's end timestamp is strictly greater than its start timestamp by at least the held duration

#### Scenario: Destruction enqueues exactly one record

- **WHEN** a span is created in a scope and destroyed by scope exit
- **THEN** exactly one record for that span drains to the sink, carrying the span's name, kind, and source location

#### Scenario: Explicit end does not double-enqueue

- **WHEN** `end()` is called explicitly and the guard is then destroyed
- **THEN** the record was enqueued once at `end()`, destruction performs no second push, and subsequent `end()` calls are no-ops

### Requirement: Wait-free span updates

`ScopedSpan` SHALL provide `set_attribute(key, value) noexcept` and `set_status(SpanStatus) noexcept` that complete in a bounded number of steps with no lock, no blocking primitive, no cross-thread atomic, and no heap allocation; updates after `end()` SHALL be no-ops that report rejection.

#### Scenario: Attributes and status land in the drained record

- **WHEN** attributes and a status are set on an active span and the record drains
- **THEN** the drained record carries every accepted attribute and the set status

#### Scenario: Updates after end are rejected

- **WHEN** `set_attribute` or `set_status` is called on an ended span
- **THEN** the call returns normally reporting rejection, mutates nothing, and the drained record is unchanged

#### Scenario: Updates never allocate or block

- **WHEN** attribute and status updates run in a tight loop with the allocation counter armed
- **THEN** zero heap allocations occur across the loop and every iteration completes in bounded steps

### Requirement: Sub-20-nanosecond enqueue

The destruction (or explicit `end()`) of a sampled span SHALL push its record into the calling thread's private SPSC shard within 20 nanoseconds (Release median over a warm loop on the target hardware), with zero heap allocations and zero locks; the push SHALL be wait-free — one bounded single-producer/single-consumer `try_push` on the caller's own shard, no CAS loop, no cross-thread atomic on the common path.

#### Scenario: Full span lifecycle meets the latency gate

- **WHEN** a warm Release loop measures complete spans — `start_span` plus destruction enqueue — and reports the median
- **THEN** the median is at most 20 nanoseconds per span, measured with the calibrated timestamp source, with the gate compiled out in Debug and sanitizer builds

#### Scenario: Enqueue performs no allocation

- **WHEN** a large batch of spans is started and ended with the allocation counter armed
- **THEN** zero heap allocations occur across the entire batch

#### Scenario: A full shard drops instead of blocking

- **WHEN** a span ends while its shard is completely full
- **THEN** the push returns immediately (bounded steps), the span is dropped and counted as `buffer_full`, and the producer thread never waits, spins, or blocks

#### Scenario: Producers are immune to consumer stalls

- **WHEN** the background consumer is momentarily stalled and producers keep ending spans
- **THEN** every producer push still completes in bounded time — stalls surface only as counted drops, never as producer blocking

### Requirement: Disabled and dropped short-circuit

When tracing is disabled or the sampling decision is `Drop`, `start_span` SHALL return an inactive span through a path costing approximately 1 nanosecond (one relaxed atomic load plus a compare): it generates no identifiers, takes no timestamp, touches no thread-local stack, and enqueues nothing on destruction; children of a dropped span are not auto-linked to it.

#### Scenario: Disabled provider emits nothing

- **WHEN** spans are started and destroyed while the provider is disabled
- **THEN** no record drains, no identifier is generated, and the thread-local active stack is untouched

#### Scenario: Sampling Drop short-circuits per span

- **WHEN** the sampling decision is `Drop` and a span is started, then the decision is changed back to `RecordAndSample` and another span is started
- **THEN** the first span produces no record and the second records normally

#### Scenario: Short-circuit latency is bounded

- **WHEN** a Release loop measures disabled `start_span` plus destruction medians
- **THEN** the median is within the short-circuit budget (approximately 1 nanosecond), materially below the recorded-span budget

#### Scenario: Dropping is not a drop counter increment

- **WHEN** spans are dropped by disable or sampling decisions
- **THEN** no `buffer_full`, `rate_limited`, or `slot_exhausted` drop is counted — sampling is a decision, not a failure

### Requirement: Cross-thread context linking

A span started with an explicit parent `SpanContext` SHALL inherit the context's trace id as its own trace id and the context's span id as its `parent_span_id`, overriding the thread's active stack, so parent-child relationships link across thread boundaries when the context is handed between threads.

#### Scenario: Child links to a context produced on another thread

- **WHEN** a span runs on thread A, its context is copied and handed to thread B, and thread B starts a span with it as the explicit parent
- **THEN** thread B's drained record carries thread A's trace id as its trace id and thread A's span id as its parent span id

#### Scenario: The child becomes the local parent

- **WHEN** a further span is started on thread B inside the explicit-parent child span without an explicit parent
- **THEN** the deeper span auto-links to the child span as its parent on thread B's stack

### Requirement: Context extraction

A live span SHALL expose `context()` returning a copyable `SpanContext` of its trace id, span id, and sampled decision, for handoff to other threads or queues.

#### Scenario: Extracted context matches the drained record

- **WHEN** a span's extracted context is compared with its later drained record
- **THEN** the context's trace id and span id equal the record's, and the sampled decision reflects the decision made at start

### Requirement: ScopedSpan serialization surface

`ScopedSpan` SHALL provide cold-path member and free `to_string()`, a `std::formatter` rendering that text, and `to_json` serializing its live state — and deliberately no `from_json`, since guards are created only by a `Tracer`.

#### Scenario: Guard renders its state

- **WHEN** `to_string()` is called on an active span and `std::format("{}", span)` is evaluated
- **THEN** both render the span's name, kind, and current status, and the free `to_string(const T&)` mirrors the member

#### Scenario: Guard is not deserializable

- **WHEN** a translation unit attempts `from_json` into a `ScopedSpan`
- **THEN** compilation fails because no overload exists
