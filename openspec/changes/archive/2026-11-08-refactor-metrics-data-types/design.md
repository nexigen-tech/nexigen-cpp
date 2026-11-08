# Design

## Context

See `proposal.md` for motivation. The file-per-entity convention (encoded in `openspec/config.yaml`) was established by the prior module refactors. This change targets the two remaining metrics files whose entities are cleanly separable value types.

## Goals / Non-Goals

**Goals:**
- One header per snapshot data type, so a consumer includes only the type it renders.
- Trim `metric_provider.hpp` to the provider engine, extracting its standalone config/binding structs.

**Non-Goals:**
- No split of instrument files (`counter.hpp`, `gauge.hpp`, `explicit_histogram.hpp`, `exponential_histogram.hpp`, `descriptive_statistics.hpp`, `state_gauge.hpp`, `meter.hpp`) — their storage cells (`CounterCell`, `HistogramBank`, `StatsBank`, …) are implementation detail of their owning instrument.
- No split of the `detail/` files or the `concepts/` files.

## Decisions

### D1 — Snapshot split with dependency order

The 7 snapshot types have a dependency chain:

```
MetricIdentity   (standalone)
ScalarValue      (standalone)
StatisticsSnapshot (standalone)
HistogramSnapshot (standalone)
StateValue       (standalone)
MetricRecord     -> MetricIdentity + the 4 value types + policy enums
MetricBatchSnapshot -> MetricRecord
```

Each type header includes only its direct dependencies. The `detail::copy_meter_identity` helper and the `max_name_length` / `max_state_length` / `max_snapshot_buckets` constants live in `metric_identity.hpp` (the identity header), since they are identity/snapshot-shape vocabulary.

The formatters (`std::formatter<T>`) follow their type into the same header (except `StateValue`, which has no standalone formatter — `MetricRecord` renders the state payload inline).

### D2 — `metric_snapshot.hpp` becomes a forwarding header

Rather than delete it and touch every consumer, `metric_snapshot.hpp` becomes a forwarding header that includes all 7 type headers. This keeps existing consumers compiling while enabling granular includes. Future cleanup can remove the forwarding header once consumers migrate.

### D3 — `metric_provider.hpp` split

- `MetricProviderConfig` (fields: worker thread, poll interval, sink list) → `metric_provider_config.hpp`, with its `std::formatter`.
- `SinkBinding` → `sink_binding.hpp`.
- `metric_provider.hpp` retains `MetricProvider` and includes the two new headers.

## Risks / Trade-offs

- **`MetricRecord` union members must stay trivially copyable**: the split must not change any field order, `alignas`, or the union layout. → Mitigation: extract content verbatim; run the full test suite.
- **The `metric_snapshot` capability spec** references the snapshot types by name; a pure file move must not alter their public spelling (`nexigen::metrics::*`). → Mitigation: keep all names/namespaces unchanged; `skip_specs` is correct since no requirement text changes.