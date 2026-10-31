# nexigen::metrics Guide

Practical, copy-paste-first guide to the metrics subsystem. The behaviour
contract lives in the `openspec/specs/metrics/*` capabilities; the public API
surface is summarised in the [README](../README.md#-nexigenmetrics-api) and the
design rationale in [architecture.md](architecture.md).

## 1. Quick start

`nexigen_metrics` is a STATIC target (alias `nexigen::metrics`) that brings
`nexigen_core` and `nexigen_chrono` transitively. Examples build by default;
`-DNEXIGEN_BUILD_EXAMPLES=OFF` / `-DNEXIGEN_BUILD_TESTS=OFF` skip them.

```cmake
add_executable(app main.cpp)
target_link_libraries(app PRIVATE nexigen::metrics)
```

The shortest complete pipeline:

```cpp
#include <chrono>
#include <nexigen/metrics/console_metric_sink.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/metric_provider.hpp>

namespace metrics = nexigen::metrics;

metrics::ConsoleMetricSink sink;                 // render batches to stdout
metrics::MetricProvider_config config{};
config.collect_interval = std::chrono::nanoseconds{50'000'000};
metrics::MetricProvider provider{config, sink};  // one per process/application
const auto meter = provider.get_meter("engine", "1.2.0");
if (!meter) { return 1; }
```

Instruments can only be created through a `Meter`, and a `Meter` only through
`get_meter` — every constructor is passkey-guarded and pinned by
`static_assert(!std::is_constructible_v<...>)`. Creation returns
`core::Result<T*>`; check `has_value()`, then use `(*result)->…`. **The pointer
lives in the provider's pre-allocated pool and is valid until the provider is
destroyed** — never keep it beyond that.

## 2. Choosing the policies

Every instrument is `create_*<Concurrency, Access, Reset>(...)`.

| Policy | Values | Choose when |
|---|---|---|
| `Concurrency` | `SingleThreaded` | exactly one recording thread AND you keep harvests off it (`collect_now()` on that thread, or explicit barriers). Plain storage — the fastest path. |
| | `MultiThreaded` | any doubt, or a background harvester runs. 33 cache-line cells: 32 exclusive `thread_local` shards + one shared fallback; wait-free per record. |
| `Access` | `Readable` | application code needs `read()` / `snapshot()` (compile-enforced). |
| | `WriteOnly` | hot-path-only counters nobody inspects in-process; only the provider harvests them. |
| `Reset` | `Cumulative` | lifetime totals (default). |
| | `ResetOnRead` | delta windows: every `read()`/`snapshot()`/harvest reports the samples since the previous observation and resets it. |

Rules of thumb:

* `MultiThreaded` + `Readable` + `ResetOnRead` is the safe interactive default.
* Never harvest a `SingleThreaded` instrument concurrently with its recorder —
  read it via `collect_now()` on the recording thread.
* `Gauge::set(v)` on a `MultiThreaded` gauge moves the **observed aggregate** to
  `v` (`fetch_add(v - Σshards)`); concurrent deltas from other threads land on
  top — this is the documented aggregate semantics, not a bug.

## 3. Thread capacity and windows

Slot registration is **per kind**: the first 32 threads that record a given kind
get exclusive `thread_local` shards; later threads share one fallback cell.
Slots are never recycled.

| Instrument | Beyond 32 threads |
|---|---|
| `Counter`, `Gauge`, both histograms | shared fallback shard — totals stay **exact** |
| `DescriptiveStatistics` | records are **dropped** and counted in `dropped_samples` (the single-writer Welford protocol cannot be shared) |

`ResetOnRead` windows are exactly-once by construction: a record racing a
window swap is counted in exactly one window (a boundary racer may land one
window later, so drain-read until two consecutive empty windows if you need the
final sum). Precision note: coarsening (wide dynamic ranges) and the
`>32`-thread fallback degrade percentile granularity — `count/sum/min/max`
stay exact, and `HistogramSnapshot::effective_scale` reports the resolution in
force.

## 4. Harvesting

Two triggers, both cold-path:

* **Background** — `config.collect_interval > 0` starts a `std::jthread`
  harvester that self-pins to `config.core_mask` (`0` inherits the process
  mask; pinning failures surface via `startup_status()`, never silently). Batches
  arrive through `sink.on_batch` at the fixed rate.
* **Deterministic** — `provider.collect_now()` harvests and delivers
  synchronously on the calling thread and returns a `core::Status`; this is what
  tests and shutdown paths should use. `provider.flush()` forwards to the sink;
  the destructor stops the harvester, performs one final harvest and flush, then
  frees the registry.

`collect_interval = 0` disables the background thread entirely.

* **Warning:** the background harvester reads *every* registered instrument on
  its own thread. `SingleThreaded` instruments use plain non-atomic storage and
  must not be harvested concurrently with recording: use `collect_now()` on the
  recording thread, or `MultiThreaded` (the safe default when a background
  harvester runs). Harvesting a `SingleThreaded` instrument while its recorder
  is live is a data race — ThreadSanitizer reports it as a race in
  `Counter_storage`/`Gauge_storage::take()`.

## 5. Sinks

The sink contract is `concepts::MetricSink`: `on_batch(const
MetricBatchSnapshot&) noexcept -> core::Status` plus `flush() noexcept`, no
virtuals. Never allocate or block in a sink callback.

* `NullMetricSink` — discard everything (benchmarks, background-only setups).
* `ConsoleMetricSink{fd = 1}` — allocation-free `::write` rendering.
* `JsonFileMetricSink{JsonFileMetricSink_config{path, append}}` — one JSON
  object per batch per line, UTC ISO-8601 stamps, `flush()` = `fsync`.
* `CompositeMetricSink<Sinks...>` — tuple fan-out without short-circuit (first
  error wins, every member still receives the batch). Construct it with the
  configured members and pull members via `get<Index>()` / `get<SinkType>()`:

```cpp
metrics::CompositeMetricSink<metrics::ConsoleMetricSink, metrics::JsonFileMetricSink,
                             metrics::NullMetricSink> sink{
    metrics::ConsoleMetricSink{},
    metrics::JsonFileMetricSink{{"/tmp/telemetry.jsonl", /*append=*/true}},
    metrics::NullMetricSink{}};
```

`MetricProvider` binds any of these at construction — the binding is type-erased
into function pointers, so the harvest loop costs one indirect call per batch.

## 6. Sizing the registry

Everything is allocated once at `MetricProvider` construction; creation and
harvesting are verified zero-allocation. Defaults (override before
construction): `max_meters=16`, `max_counters=64`, `max_gauges=64`,
`max_statistics=32`, `max_explicit_histograms=16`, `max_exponential_histograms=8`
— roughly 3.3 MiB with defaults (histogram slots dominate). Exhaustion returns
`core::ErrorCode::BufferFull` at the failing `create_*` call; invalid names
(over 63 chars, duplicates — including across kinds in one meter) and bad
histogram parameters (unsorted/non-finite bounds, scale outside 0–20) return
`InvalidArgument` and consume no slot.

## 7. Real-time checklist

* No `to_string()` / `std::string` / `nlohmann::json` / `std::format` on a
  recording path or in a sink callback — all diagnostics, all cold.
* Branch on `Result`: `has_value()` / `*res` / `.error()` — never `.value()`.
* Recording is wait-free on `MultiThreaded` shards (`relaxed` RMW on the
  caller's own cache line); a record costs a handful of nanoseconds — pinned by
  the Release latency gates (`st_inc 2 ns · mt_inc 6 ns · gauge add 6 ns · gauge
  set 8 ns · stats 25 ns · histograms 15 ns` medians).
* Create instruments during startup (before the hot loop); the registry is
  append-only and never resized.
* Keep handles inside the provider's lifetime; a stale `Meter`/instrument pointer
  is a lifetime violation (provider entry points detect it and return
  `NotInitialized`).

## 8. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `create_*` returns `BufferFull` | raise the corresponding `max_*` capacity in `MetricProvider_config` |
| `create_*` returns `InvalidArgument` | name > 63 chars, duplicate name (across kinds in one meter), unsorted/non-finite histogram bounds, or exponential scale outside 0–20 |
| `get_meter`/`collect_now` after destruction → `NotInitialized` | you held the provider or its handles past its lifetime |
| `startup_status()` → `SystemError` | harvester could not pin `core_mask` — pick CPUs inside the process's allowed set, or use `0` |
| No background batches | `collect_interval == 0` — the thread is disabled by design; call `collect_now()` |
| TSan race in `Counter_storage`/`Gauge_storage::take()` | a `SingleThreaded` instrument is harvested by the background thread while its recorder is live — switch to `MultiThreaded`, or harvest with `collect_now()` on the recording thread |
| Statistics drop samples under load | more than 32 threads record that kind concurrently — check `dropped_samples`, or reduce the thread set |
| Percentiles look coarse after a wide range | coarsening lowered `effective_scale`; counts stay exact, resolution degrades as documented |
