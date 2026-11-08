# Proposal

## Why

`metric_snapshot.hpp` packs **7 standalone value types** (`MetricIdentity`, `ScalarValue`, `StatisticsSnapshot`, `HistogramSnapshot`, `StateValue`, `MetricRecord`, `MetricBatchSnapshot`) plus 7 `std::formatter` specializations into a single header. These are pure data types — each is just fields + a `to_string()` — with no shared state or cross-dependency (aside from `MetricRecord` aggregating the others, and `MetricBatchSnapshot` wrapping a batch of records). A consumer needing only `HistogramSnapshot` today includes all 7 types plus the batch machinery.

The previous module refactors established the file-per-entity convention; this change applies it to the metrics snapshot layer — the largest remaining cluster of cleanly separable types in the project.

## What Changes

- Split `metric_snapshot.hpp` (298 lines, 14 entities) into per-type headers:
  - `metric_identity.hpp` — `MetricIdentity` + its `detail::copy_meter_identity` helper + `std::formatter<MetricIdentity>`.
  - `scalar_value.hpp` — `ScalarValue` + `std::formatter<ScalarValue>`.
  - `statistics_snapshot.hpp` — `StatisticsSnapshot` + `std::formatter<StatisticsSnapshot>`.
  - `histogram_snapshot.hpp` — `HistogramSnapshot` + `std::formatter<HistogramSnapshot>`.
  - `state_value.hpp` — `StateValue` (no formatter — `MetricRecord` renders it inline).
  - `metric_record.hpp` — `MetricRecord` + `std::formatter<MetricRecord>` (includes all the above).
  - `metric_batch_snapshot.hpp` — `MetricBatchSnapshot` + `std::formatter`.
  - Trimmed `metric_snapshot.hpp` becomes a forwarding header (includes all 7 type headers) for backward compatibility, or is deleted with all include sites updated.
- Split `MetricProviderConfig` and `SinkBinding` out of `metric_provider.hpp` (311 lines) into:
  - `metric_provider_config.hpp` — the standalone config struct + `std::formatter`.
  - `sink_binding.hpp` — the binding struct.
  - Trimmed `metric_provider.hpp` — just `MetricProvider` + its helpers.
- Update `#include` sites across `metrics/**`, `app`, and any other consumers of these types.

**Not in scope:** instrument files (`counter.hpp`, `gauge.hpp`, `explicit_histogram.hpp`, etc.) — each is a coherent implementation unit (storage cells + class + formatter) where the storage cells are functionally nested implementation detail of the owning instrument. Per the convention: "Private nested types … stay nested inside the owning type."

## Capabilities

Pure refactor — `skip_specs: true`.

## Impact

- **Code**: ~298-line `metric_snapshot.hpp` → 7 type headers + forwarding; ~311-line `metric_provider.hpp` → config + binding headers + trimmed provider. No behavior, layout, or performance change.
- **Dependencies**: include-graph only. Consumers can include individual snapshot types instead of the whole batch layer.
- **Performance & memory**: zero — pure header reorganization; all `alignas(64)` layouts and `constexpr` / `inline` qualifiers preserved.