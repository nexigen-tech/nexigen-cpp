# Spec Delta

## MODIFIED Requirements

### Requirement: CPU-pinned background consumer

`TracerProvider` SHALL run its existing background consumer (`std::jthread`) with a configured `ThreadSpec` instead of a uint64 core mask. The consumer SHALL self-apply the spec before `binding_.start` and before its first delivery, supporting CPUs above 63, naming, and Other/FIFO/RR scheduling; a zero/empty set inherits OS placement. It SHALL drain registered shards round-robin with bounded slices while producers never wait. Best-effort failures SHALL remain visible without stopping delivery; required failures SHALL make `initialize()` fail and join the worker before its drain loop. Idle behavior SHALL retain `core::WorkerTuning` unchanged.

#### Scenario: Worker affinity matches the configured mask

- **WHEN** a provider has valid worker attributes and its consumer delivers a span
- **THEN** the requested name, affinity, policy, and priority were applied before that delivery and are reflected in the startup report

#### Scenario: CPU above 63 is supported

- **WHEN** the worker spec contains a permitted CPU above 63
- **THEN** the consumer uses it without truncating to the compatibility mask

#### Scenario: Pin failures are visible without stopping the pipeline

- **WHEN** an affinity or scheduler request fails under `BestEffort`
- **THEN** the startup snapshot reports the failure, the consumer still runs with any applied attributes, and spans drain

#### Scenario: Required failure rolls back initialization

- **WHEN** an attribute request fails under `Required`
- **THEN** `initialize()` returns `SystemError`, the sink is not started, the worker does not enter its drain loop, and destruction is safe

#### Scenario: Idle consumer wake-ups are bounded by the ceiling

- **WHEN** a provider runs idle with the default ceiling
- **THEN** its consumer thread performs on the order of one wake-up per ceiling interval (≈1 ms), not one per 200 µs, and a raised ceiling reduces the rate proportionally

#### Scenario: Per-shard FIFO and fair drain hold

- **WHEN** multiple producer threads record spans concurrently and the sink drains them
- **THEN** each thread's spans drain in that thread's start order, and no shard is starved by a flooded sibling

#### Scenario: Producers never wait on the worker

- **WHEN** the worker is momentarily delayed and producers keep ending spans
- **THEN** every producer operation completes in bounded time — delays surface only as counted drops, never as blocking

### Requirement: Provider serialization surfaces

`TracerProvider`'s configuration SHALL provide member/free `to_string`, full `to_json`/`from_json` round-trip, and `std::formatter`, including its complete worker thread spec; the provider, its `Tracer` handles, and its drop snapshot SHALL provide member/free `to_string` and `std::formatter`, plus `to_json` with deliberately no `from_json` since they are factory-bound live objects.

#### Scenario: Provider config round-trips through JSON

- **WHEN** a configuration (worker thread spec with CPUs above 63 and RR priority, tracer pool size, span-rate budget, worker poll interval) is serialized and parsed back
- **THEN** every field survives the round-trip under `JSON_NOEXCEPTION=1`, and `std::format` renders the `to_string()` text

#### Scenario: Handles and snapshots render diagnostics

- **WHEN** `to_string()` is evaluated on the provider, a tracer, and a drop snapshot
- **THEN** the provider renders its configuration and state, the tracer renders its scope name, the snapshot renders per-cause counts, and in each case `std::format("{}", value)` equals the member's text

#### Scenario: Live provider objects are not deserializable

- **WHEN** a translation unit attempts `from_json` into a `TracerProvider`, `Tracer`, or the drop snapshot type where none exists
- **THEN** compilation fails for the provider and tracer (no overload exists), while the drop snapshot — a value type — parses successfully
