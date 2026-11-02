# Design

## Context

The hub's embedded UI (`src/hub/web_assets.cpp`, `app_js()`) subscribes to a stream over SSE and
appends each `record.line` verbatim to the viewer (`appendLine`). The metrics stream is JSONL where
each line is one collection batch: `{"timestamp":..., "records":[...]}`, and each record carries a
`kind` (`Counter`/`Gauge`/`DescriptiveStatistics`/`ExplicitHistogram`/`ExponentialHistogram`/`State`)
plus kind-specific fields. Separately, `JsonFileMetricSink::on_batch` serializes `Counter`, `Gauge`,
and `DescriptiveStatistics` explicitly but lets `MetricKind::State` fall through to the histogram
`default`, reading the state string's bytes as a histogram (garbage `count`/`sum`/…, no `value`).

## Goals / Non-Goals

**Goals:**

- Make the metrics stream readable: one compact line per harvested metric, formatted by kind.
- Serialize state records correctly so the state kind actually reaches the viewer.
- Keep other streams raw, and keep a raw fallback for unparseable lines.

**Non-Goals:**

- No dashboard, charts, or time-series view (deferred to a follow-up).
- No units/dimension metadata (the metric model has none; deferred).
- No server, API, discovery, or streaming change.

## Decisions

### D1 — Serialize State records in the sink

Add a `case MetricKind::State` to `JsonFileMetricSink::on_batch` that writes
`"value":"<escaped state string>"` (via `detail::view_name(record.state.value)`), and cover it in
the `test_sink_kind_matrix` kind matrix.

- *Rationale:* the state value is a bounded inline string; it must be escaped like names and, as the
  last field, followed only by the closing `}`.
- *Alternative considered:* leave State serialized as a histogram. Rejected: it emits garbage and
  makes the state kind unusable downstream (including the hub).

### D2 — Render client-side in the existing viewer

The SSE `record` handler, when the selected stream's kind is `metrics`, parses `record.line` as JSON
and renders each record in the batch; otherwise it appends the raw line as today.

- *Rationale:* the data already flows to the browser; no new endpoint or server work is needed.
- *Alternative considered:* a server-side formatted payload. Rejected: it expands the HTTP surface.

### D3 — Dispatch on the metric `kind`

For each record in the batch, format by kind:

- Counter/Gauge → `name = value`.
- DescriptiveStatistics → `name  count=N min=… max=… mean=… std=…`.
- Histogram → `name  n=N min=… max=… p50=… p90=… p99=… p99.9=…`.
- State → `name = value`.

- *Rationale:* mirrors the six `MetricKind` values and keeps each line scannable.

### D4 — Keep the raw JSON fallback

If `JSON.parse` fails or the parsed object has no `records` array, append the original line unchanged.

- *Rationale:* a malformed or oversized line must stay inspectable rather than vanish.

## Risks / Trade-offs

- [Rendering only the latest window] The viewer already streams all batches; rendering per batch
  keeps history but grows the DOM. → Mitigation: the existing `maxLines` cap still applies.
- [Key drift] The client depends on the JSONL field names the metrics sink writes. → Mitigation:
  the format is produced by the metrics module and covered by the sink kind-matrix test; a wrong
  `kind` value falls through to the raw fallback.

## Migration Plan

Not applicable — a sink serialization fix and a client-side rendering change with no persisted
format or API change; a rebuild picks both up.

## Open Questions

None.
