# Proposal

## Why

Metrics carry only raw numeric values, so the hub renders `latency = 10200000` with no indication
that the value is nanoseconds. Adding a per-instrument unit (a short free-form string such as
`ns`, `bytes`, `requests/s`) lets consumers — the JSONL sink and the hub viewer — render values
with their meaning attached.

## What Changes

- Add an optional `unit` string to the `MetricIdentity` and to the `Meter` creation methods
  (`create_counter`, `create_gauge`, `create_statistics`, `create_state`,
  `create_explicit_histogram`, `create_exponential_histogram`), stored in the fixed-size identity
  and round-tripped through JSON.
- Serialize a non-empty unit in the JSON-lines sink, and render it in the hub metric viewer.

## Capabilities

### Modified Capabilities

- `metrics/metric-provider`: instruments may declare a unit, stored in the record identity and
  serialized with it.

## Impact

- `include/nexigen/metrics/metric_snapshot.hpp` (identity field), `meter.hpp` /
  `metric_provider.hpp` (creation signatures and bodies), `src/metrics/snapshot_format.cpp`
  (JSON), `include/nexigen/metrics/json_file_metric_sink.hpp` (JSONL output),
  `src/hub/web_assets.cpp` (rendering), and the metrics tests.
- Performance, latency, and memory allocation: one extra fixed-size `char[64]` per instrument slot
  (no heap); recording/harvest hot paths are unchanged. The creation and serialization changes are
  cold-path only.
