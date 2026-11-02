# Proposal

## Why

Two related gaps make hub metrics hard to use. First, the web UI renders the metrics stream as
dense raw JSONL, so a counter value, a histogram's percentiles, or a state are buried in JSON and
hard to read. Second, the `JsonFileMetricSink` never serializes state records: its
`switch (record.kind)` handles `Counter`, `Gauge`, and `DescriptiveStatistics` and routes
everything else — including `MetricKind::State` — through the histogram `default`, which reads the
state string's bytes as a histogram and emits a garbage `count`/`sum`/… with no `value`. The
metric payload is already structured server-side; both the sink and the client just need to treat
the state kind correctly.

## What Changes

- Fix `JsonFileMetricSink::on_batch` to serialize `MetricKind::State` records as a `"value"`
  JSON string, and cover it in the sink kind-matrix test.
- Render the metrics stream in a compact, kind-aware form in the existing web viewer:
  counters/gauges as `name = value`, descriptive statistics as count/min/max/mean/std, histograms
  as count/min/max plus percentiles, and state gauges as `name = value`.
- Keep all other streams rendering as raw text, and fall back to raw JSON when a line does not
  parse.

## Capabilities

### Modified Capabilities

- `metrics/metric-sinks`: state records serialize as a `"value"` string instead of a histogram
  payload.
- `hub/web-ui`: metrics streams render structured by metric kind instead of as raw JSONL text.

## Impact

- `include/nexigen/metrics/json_file_metric_sink.hpp` (one `switch` case) and its
  `tests/metrics/metrics_test.cpp` kind matrix; `src/hub/web_assets.cpp` (client JS) and
  `tests/hub/hub_test.cpp`. No HTTP API, discovery, or streaming change.
- Performance, latency, and memory allocation: the sink fix adds one bounded escaped string write
  on the cold sink path (the state value is at most 63 bytes); the client change adds browser-only
  JSON parsing/rendering.
