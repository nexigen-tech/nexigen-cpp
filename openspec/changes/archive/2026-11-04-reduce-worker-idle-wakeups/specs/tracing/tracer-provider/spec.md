# Spec Delta

## Purpose

The tracing consumer's idle behaviour moves from the hard-coded 64-sweep spin + 200 µs sleep to the shared `core::WorkerTuning` policy with a configurable ceiling, and the config surface round-trips the new field.

## MODIFIED Requirements

### Requirement: CPU-pinned background consumer

`TracerProvider` SHALL run one background consumer worker (`std::jthread`) that self-pins to the configured `core_mask` via `pthread_setaffinity_np` before its first delivery (a zero mask inherits OS placement), drains all registered shards round-robin with a bounded per-sweep slice so one flooded shard cannot starve the others, and delivers each record to the bound sink — while producers never wait on the worker; idle periods use bounded backoff from `core::WorkerTuning` (a short spin then a sleep that doubles up to the configured `worker_poll_interval_us`, default 1000 µs, with a widened timer slack so co-located consumers coalesce) so a pinned core is not burned busy-polling.

#### Scenario: Idle consumer wake-ups are bounded by the ceiling

- **WHEN** a provider runs idle with the default ceiling
- **THEN** its consumer thread performs on the order of one wake-up per ceiling interval (≈1 ms), not one per 200 µs, and a raised ceiling reduces the rate proportionally

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

### Requirement: Provider serialization surfaces

`TracerProvider`'s configuration SHALL provide member/free `to_string`, full `to_json`/`from_json` round-trip, and `std::formatter`; the provider, its `Tracer` handles, and its drop snapshot SHALL provide member/free `to_string` and `std::formatter`, plus `to_json` with deliberately no `from_json` since they are factory-bound live objects.

#### Scenario: Provider config round-trips through JSON

- **WHEN** a configuration (core mask, tracer pool size, span-rate budget, worker poll interval) is serialized and parsed back
- **THEN** every field survives the round-trip under `JSON_NOEXCEPTION=1`, and `std::format` renders the `to_string()` text

#### Scenario: Handles and snapshots render diagnostics

- **WHEN** `to_string()` is evaluated on the provider, a tracer, and a drop snapshot
- **THEN** the provider renders its configuration and state, the tracer renders its scope name, the snapshot renders per-cause counts, and in each case `std::format("{}", value)` equals the member's text

#### Scenario: Live provider objects are not deserializable

- **WHEN** a translation unit attempts `from_json` into a `TracerProvider`, `Tracer`, or the drop snapshot type where none exists
- **THEN** compilation fails for the provider and tracer (no overload exists), while the drop snapshot — a value type — parses successfully
