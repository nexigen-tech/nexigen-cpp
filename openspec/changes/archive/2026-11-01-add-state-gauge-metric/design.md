# Design

## Context

The metrics subsystem (`nexigen::metrics`) publishes fixed-size, trivially copyable `MetricRecord` values through a pre-allocated, zero-allocation registry (see `metrics/metric-provider` and `metrics/counter-gauge` specs). Every instrument is a value type created only through a `Meter`, with a `MetricKind` classification and a kind-specific payload in `MetricRecord`'s union (`ScalarValue`, `StatisticsSnapshot`, `HistogramSnapshot`).

The system has no string-valued instrument today, so named states (modes, phases, enum spellings) cannot be surfaced as strings. See proposal.md — Why.

## Goals / Non-Goals

**Goals:**
- A `StateGauge` instrument that records a current named state as a bounded string, last-writer-wins, with `SingleThreaded` plain storage and `MultiThreaded` seqlock storage.
- The state survives the existing harvest/serialize path (`MetricRecord` → `to_json` → sink) with no heap allocation and no `MetricRecord` size change.

**Non-Goals:**
- No state history, transition counting, or duration tracking — this is a current-value metric only.
- No typed enum template parameter — the value is a plain `std::string_view`; callers render enums with the project-wide `to_string(Enum)`.
- No `from_json` for the instrument (Meter-only creation, like the other instruments).

## Decisions

### D1 — Inline bounded buffer, length 63

The state lives in `char value_[max_state_length + 1]` with `max_state_length == max_name_length == 63`. `set(std::string_view)` copies with a bounded `memcpy` and rejects (returns `false`, no side effect) when the value is empty or longer than 63 characters.

- *Why:* preserves the zero-allocation, trivially-copyable `MetricRecord` invariant; 63 is the established name bound and state spellings are short.
- *Alternative:* `std::string` value — rejected: heap allocation on the record path, breaks trivially-copyable.

### D2 — New union member, no size change

`MetricRecord`'s union gains a `StateValue` member (`char value[max_state_length + 1]`), selected by `MetricKind::State`. The union is already sized to `HistogramSnapshot`, so the record's `sizeof` is unchanged.

- *Why:* the state rides the existing snapshot/serialization machinery without widening the record.
- *Alternative:* widen `ScalarValue` with a string — rejected: muddies the counter/gauge scalar semantics.

### D3 — `std::string_view` API

`set(std::string_view)` and `read() -> std::string_view`. Enum values are passed as `set(to_string(enum))` (the project-wide mandate 9 spelling), so the metric system never depends on a specific enum type.

- *Alternative:* templated `set(Enum)` with automatic rendering — rejected for this change: couples the type-erased registry to arbitrary enum types.

### D4 — Concurrency model

- `SingleThreaded`: a plain (non-atomic) `char[N]` cell; `set` is a `memcpy`. Single-writer contract, matching `Counter`/`Gauge` SingleThreaded (reads/harvests must not race recording without a happens-before edge).
- `MultiThreaded`: a seqlock — an `std::atomic<std::uint64_t>` sequence counter plus the buffer. A writer serializes via a spinlock (control-plane `set`, bounded), writes the buffer, then publishes the sequence. Readers (the harvester / `read()`) loop on the sequence: read even, copy buffer, re-read sequence; retry on mismatch, so they never observe a torn value.

- *Why:* seqlock gives wait-free readers (the harvester is the hot side) without a `std::mutex`; the writer spinlock is cold-path and bounded, honoring mandate 5.
- *Alternative:* `std::mutex` around the buffer — simpler but introduces a blocking primitive; rejected in favor of the seqlock pattern already used by `DescriptiveStatistics`.
- *Alternative:* per-thread sharding — rejected: a "current value" has no aggregate to shard.

### D5 — Reset windows

`ResetPolicy` is retained (the `Metric` concept requires the static `reset`). `Cumulative` keeps the value until the next `set`; `ResetOnRead` reports the value and then clears it to empty (the clear is itself a seqlock write in `MultiThreaded`).

### D6 — Factory and capacity

`Meter::create_state(name)` mirrors `create_counter`/`create_gauge`, returning `core::Result<StateGauge*>`; `MetricProviderConfig` gains `max_states` (a `uint32_t` capacity like the other families), and the registry gains a sixth kind pool (`kind_count` 5 → 6).

## Risks / Trade-offs

- [Over-long or empty state silently rejected] → `set` returns `false`; documented in the spec; callers can treat it as a configuration error.
- [Concurrent `set` tearing under MultiThreaded] → seqlock with writer serialization; the spec pins a "never observe a torn value" scenario.
- [`ResetOnRead` racing a `set`] → both go through the seqlock; a clear and a set that overlap serialize on the writer lock, so a window reports exactly one complete value.
- [Record ABI / enum stability] → `MetricKind::State` is append-only; no existing field changes; `MetricRecord` `sizeof` is unchanged.

## Migration Plan

No migration: the change is additive. `MetricKind::State` is append-only, existing instruments and consumers are untouched, and the JSON output only gains a new `"state"` field for state records.
