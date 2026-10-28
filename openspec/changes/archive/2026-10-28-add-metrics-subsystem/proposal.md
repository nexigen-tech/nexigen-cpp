# Proposal

## Why

`nexigen::metrics` is the last empty module in the ecosystem, and real-time HIL platforms need observability that never perturbs the simulation loop: counter/gauge updates must cost sub-nanosecond to low-nanosecond time, recording must not allocate or take locks, and background collection must run on isolated CPU cores. Delivering this now, on top of the existing `nexigen::core` error model, `nexigen::chrono` timestamps, and the `nexigen::logging` affinity precedent, keeps every module on one serialization, formatting, and concurrency contract before downstream consumers adopt metrics APIs.

## What Changes

- Introduce the `nexigen::metrics` policy enums `ConcurrencyPolicy`, `AccessPolicy`, `ResetPolicy`, and `MetricKind`, each with `constexpr to_string`/`from_string`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter` specializations.
- Establish the module-wide serialization contract: every public class, struct, snapshot, and config implements cold-path `std::string to_string() const` plus a free `to_string(const T&)`, with non-throwing `nlohmann::json` round-trips and `std::formatter` support where the type is deserializable or an enum.
- Add the factory hierarchy where instruments are constructible only through `Meter`, and `Meter` only through `MetricProvider::get_meter(name, version)`; construction is guarded by a private `MeterPasskey`/`friend class Meter` and pinned by `static_assert(!std::is_constructible_v<...>)`.
- Add a pre-allocated, cache-line-aligned (`alignas(64)`) metric registry inside `MetricProvider`, so instrument creation, hot-path recording, and harvesting never touch the heap and registry traversal is contiguous in memory.
- Add C++23 `concepts::Metric` and `concepts::ReadableMetric`; `AccessPolicy::Readable` exposes `.read()`/`.snapshot()`, while `AccessPolicy::WriteOnly` rejects them at compile time via `requires (Access == AccessPolicy::Readable)` and leaves harvesting to provider friend access.
- Add `Counter` and `Gauge` instruments: single-threaded plain storage (~0.5 ns `inc()`), multi-threaded cache-line-aligned per-thread shards (up to 32 registered threads, wait-free, no cross-core CAS), and `Cumulative` / `ResetOnRead` windows.
- Add `DescriptiveStatistics` with O(1), allocation-free online `count`/`min`/`max`/`mean`/`std` (Welford), and wait-free double-buffered active/shadow banks per thread so `ResetOnRead` windows swap without blocking or losing concurrent `record(double)` calls.
- Add `ExplicitBucketHistogram` with caller-supplied inline bucket bounds and `ExponentialHistogram` with base-2 exponential mapping computed by bitwise IEEE-754 extraction (`std::bit_cast`), both reporting bucket counts, `count`, `sum`, `min`, `max`, and `p50`/`p90`/`p99`/`p99.9`.
- Add the `concepts::MetricSink` contract and the `NullMetricSink`, `ConsoleMetricSink` (raw `::write`), `JsonFileMetricSink` (JSON-lines with `nexigen::chrono` UTC ISO-8601 timestamps), and variadic `CompositeMetricSink<Sinks...>` fan-out implementations.
- Add background harvesting in `MetricProvider`: a `std::jthread` harvester configured by `MetricProvider_config` (`collect_interval`, `core_mask`), self-pinned with `pthread_setaffinity_np`, plus `collect_now()` and `flush()` control-plane entry points for deterministic testing and clean shutdown.
- Add `tests/metrics/metrics_test.cpp` and its CTest registration. The runnable `examples/metrics/` executables and the `examples` catalogue expansion are owned by the `add-metrics-examples` change and are not part of this change.

## Capabilities

### New Capabilities

- `metrics/policy-enums`: policy enum catalogue plus the module-wide `to_string`/`from_string`, `nlohmann::json`, and `std::formatter` contract for every public metrics type.
- `metrics/metric-concepts`: `Metric` / `ReadableMetric` structural concepts and compile-time read-access gating.
- `metrics/metric-provider`: provider/meter factory hierarchy, passkey construction, pre-allocated registry, background harvesting, CPU affinity, collection period, and control-plane operations.
- `metrics/metric-sinks`: `MetricSink` concept and the null, console, JSON-file, and composite sink implementations.
- `metrics/counter-gauge`: `Counter` and `Gauge` hot-path instruments across concurrency, access, and reset policies.
- `metrics/descriptive-statistics`: allocation-free online moment statistics with cumulative and reset-on-read windows.
- `metrics/explicit-histogram`: explicit-bucket histogram recording and percentile snapshots.
- `metrics/exponential-histogram`: base-2 exponential histogram with O(1) bitwise bucket mapping.

### Modified Capabilities

<!-- intentionally empty: the examples catalogue change moved to add-metrics-examples -->

## Impact

- **New public headers**: `include/nexigen/metrics/` — `policy_enums.hpp`, `concepts/metric_concept.hpp`, `concepts/metric_sink_concept.hpp`, `metric_snapshot.hpp`, `meter.hpp`, `metric_provider.hpp`, `counter.hpp`, `gauge.hpp`, `descriptive_statistics.hpp`, `explicit_histogram.hpp`, `exponential_histogram.hpp`, and the four sink headers, plus `detail/` helpers (affinity, thread slots, fixed JSON writer).
- **New sources**: `src/metrics/` for cold paths — provider lifecycle/harvest loop, affinity, sink I/O, and snapshot/record serialization. All templated hot paths stay inline in headers.
- **New build target**: `nexigen_metrics` (`nexigen::metrics`) linking `nexigen_core` and `nexigen_chrono` publicly, `Threads::Threads` privately, with `_GNU_SOURCE` for affinity APIs.
- **New tests**: `tests/metrics/metrics_test.cpp` registered as `nexigen_metrics_tests` with Release-only latency gates.
- **Performance**: single-threaded `Counter::inc()` targets ~0.5 ns; multi-threaded sharded updates target 1–3 ns and are wait-free; gauge and histogram recording target low single-digit nanoseconds; `DescriptiveStatistics::record()` is O(1); exponential bucket mapping is O(1) bit manipulation with no `std::log`. Release builds pin these with median latency gates (leaving headroom over the targets); Debug and sanitizer builds run correctness only.
- **Memory allocations**: zero heap allocations during instrument creation, hot-path recording, harvesting, and shutdown. All instrument storage, per-thread shards, and batch buffers are pre-allocated once during `MetricProvider` initialization (one-time allocation at startup is allowed by mandate 4 and is sized by configuration). `std::string`/`nlohmann::json` stay cold-path only (snapshots, sink flush, diagnostics).
- **Dependencies**: no new third-party dependency; reuses `nlohmann::json` provided transitively by `nexigen_core` and the `pthread`/`sched` surface already used by `nexigen::logging`.
- **Compatibility**: additive; no existing public API or spec requirement is removed. The `examples` capability is not touched by this change.
