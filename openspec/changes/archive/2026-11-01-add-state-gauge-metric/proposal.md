# Proposal

## Why

Real-time and HIL applications need to surface operational state — mode, phase, health, enum-valued signals — through the metrics pipeline, but the existing instruments (`Counter`, `Gauge`, `DescriptiveStatistics`, histograms) only carry numeric values. Today a caller must map an enum to an integer and the dashboard must map it back to a name. A "current state" metric lets an operator observe named states directly (`"active"`, `"degraded"`, `"running"`) without either mapping.

## What Changes

- Add a new instrument `nexigen::metrics::StateGauge<Concurrency, Access, Reset>` that records a current named state as a bounded string (`set(std::string_view)`, `read()`, `snapshot()`), last-writer-wins.
- Add `MetricKind::State` to the metric-kind vocabulary (append-only), with the full `to_string`/`from_string`/`to_json`/`from_json`/`std::formatter` contract.
- Extend `MetricRecord`'s union with a fixed-size inline string buffer, so a harvested record can carry the state without heap allocation.
- Extend the provider with a `create_state(...)` factory and a `max_states` capacity, mirroring the existing instrument families.
- Add unit tests and a runnable example. No breaking changes: the enum change is append-only and existing instruments are untouched.

## Capabilities

### New Capabilities
- `metrics/state-gauge`: a current-state instrument that records a bounded named state (string) with `set`/`read`/`snapshot`, `SingleThreaded` plain storage and `MultiThreaded` last-writer-wins (seqlock), and the full serialization contract.

### Modified Capabilities
- `metrics/policy-enums`: `MetricKind` gains the `State` enumerator (canonical spelling, parse, JSON, formatter).
- `metrics/metric-provider`: the provider gains a `create_state` factory and `max_states` capacity; `MetricRecord` carries a state string and serializes it.

## Impact

- **Code**: new `include/nexigen/metrics/state_gauge.hpp`; modified `policy_enums.hpp`, `metric_snapshot.hpp`, `meter.hpp`, `metric_provider.hpp`, `detail/layout.hpp`, `src/metrics/snapshot_format.cpp`, `src/metrics/provider.cpp`.
- **Tests/examples**: `tests/metrics/metrics_test.cpp`; a new `examples/metrics/` example.
- **Performance**: recording is control-plane (set on state transitions, not a hot path). `set()` is a bounded `memcpy` into a fixed inline buffer — zero heap allocation, zero lock on the `SingleThreaded` path; `MultiThreaded` uses a seqlock (no mutex, no blocking). `MetricRecord` size is unchanged (the union is already sized to `HistogramSnapshot`).
- **Memory**: no allocation anywhere; the string lives in a 64-byte inline buffer, like the existing identity name fields.
