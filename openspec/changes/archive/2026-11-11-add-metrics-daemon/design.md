# Design

## Context

`MetricProvider` already runs a background harvester (`std::jthread`) that delivers a `MetricBatchSnapshot` to a duck-typed `MetricSink` (`on_batch`/`flush`, no virtual dispatch) every `collect_interval`. All math is done client-side: `StatisticsSnapshot` carries `mean`/`stddev`, `HistogramSnapshot` carries `p50/p90/p99/p999` and raw per-bucket counts, and `ResetOnRead`/`SeqLock` are handled by the instrument harvest thunks. The missing piece is a cross-process consumer — today every sink is in-process. This change mirrors the logging pipeline (`Iceoryx2Sink` publishes `LogFrame`; `nexigen-logd` subscribes) with a metric frame, a publishing sink, and a daemon. See proposal.md — Why.

## Goals / Non-Goals

**Goals:**
- A fixed, trivially-copyable cross-process wire type for one harvested metric.
- A client sink that publishes batches over iceoryx2 without touching the producer hot path.
- A daemon that aggregates, renders Prometheus text, serves it over HTTP, and appends a quota-bounded JSON-lines history — all allocation-free on the drain loop.

**Non-Goals:**
- OS telemetry (`/proc`, cgroups, thermal), PHM threshold alerting, and re-publishing an aggregate summary back onto iceoryx2 (separate additive changes).
- Compressed/binary disk format (plain JSON-lines for the MVP).
- Any change to `MetricProvider`, the instruments, or the existing in-process sinks.

## Decisions

### D1 — Wire type is one frame per record

`MetricFrame` is a fixed POD published once per harvested record; the batch is fanned out into N frames by the sink.

```
offset  size  field
0       4     app_id          4-byte ASCII code (e.g. "LIDR")
4       4     pid             publisher PID (::getpid(), cached at start)
8       8     timestamp_ns    window stamp (CLOCK_MONOTONIC_RAW)
16      ~4432 record          MetricRecord (trivially copyable, ~4.4 KB)
```

`MetricRecord` is ~4.4 KB dominated by `HistogramSnapshot` (`buckets_lower[256]` + `buckets_count[256]` = 4 KB) and is naturally 8-aligned, so it starts at offset 16 with no padding. The frame is fixed-size regardless of record count; `static_assert(std::is_trivially_copyable_v<MetricFrame>)` holds because `MetricRecord` already asserts it. `alignas` is left at the member's natural 8; the frame is published through a zero-copy `loan_uninit`/`write_payload`.

- *Alternative rejected:* publish the whole `MetricBatchSnapshot` as one variable-size sample. `com::Publisher<T>` is templated on a fixed POD and `MetricBatchSnapshot` is pointer-based (its `records` pointer does not survive process boundaries), so the batch would need manual serialization and a variable-size payload — more machinery for no correctness gain.
- *Alternative rejected:* a frame with a fixed `records[N]` array. Either wastes shared memory for small batches or truncates large ones; per-record frames match the `LogFrame` publish-on-`handle` pattern exactly.

### D2 — One shared pub-sub service + event service

The sink publishes to a single configured service (`nexigen/metrics/stream`) and notifies on `<service>Event`, exactly like `Iceoryx2Sink`. The `app_id` disambiguates producers inside the frame rather than per-app services.

Because one batch fans out to one frame per record, the service is created with an explicit `subscriber_max_buffer_size` (default 256) on both the sink's publisher and the daemon's subscriber. The transport default is two samples (`iceoryx2 config: subscriber_max_buffer_size = 2`), so without this every process with more than two metrics would lose frames deterministically. This is the only `com` change: an optional third/fourth `create` argument, with `0` preserving the old behavior.

- *Alternative rejected:* one service per application. More services to configure and discover for no benefit; the daemon already keys aggregation on `app_id`.

### D3 — App identity is the 4-byte `app_id`, not the 63-byte app name

`MetricFrame` carries a 4-byte `app_id` (mirroring `LogFrame.app_id` and the Prometheus example `app="LIDR"`), sourced from `Iceoryx2MetricSinkConfig.app_id`. The richer `app_name` (≤63 B) remains a property of the in-process `JsonFileMetricSink`, not the wire.

- *Alternative rejected:* a 63-byte app name in the frame. More consistent with `JsonFileMetricSink`, but bloats the fixed frame and diverges from the established iceoryx2 transport (`LogFrame`) that the daemon pattern is mirrored from.

### D4 — Aggregation is a single-writer, pre-allocated table

The daemon's drain thread is the sole writer, so the latest-record table needs no atomics. It is a fixed-capacity open-addressing table (capacity = `max_metrics`), keyed by a hash of `(app_id, meter_name, metric_name)` with linear probing, pre-allocated at startup. Each slot holds the full `MetricRecord`. On insert, the slot is overwritten (latest-wins). No `std::map`/`std::unordered_map`/heap growth.

### D5 — HTTP runs on a separate thread over a double-buffered exposition snapshot

The iceoryx2 `WaitSet` owns its own epoll, so the TCP listen socket cannot share the drain thread. A dedicated `std::jthread` runs a minimal epoll responder (reusing `hub::detail::http_parse.hpp` for request-line/header parsing):

- The drain thread renders the full exposition into one of two `alignas(64)` pre-allocated buffers on the batch timer, then publishes it with `published_.store(&buf, std::memory_order_release)`.
- The HTTP thread reads `published_.load(std::memory_order_acquire)` and writes those bytes on `GET /metrics`.

Double-buffering means the HTTP thread never observes a half-rendered buffer; release/acquire makes the render visible. The aggregation table itself is never touched by the HTTP thread. `alignas(64)` on the buffers prevents false sharing with the published pointer.

- *Alternative rejected:* reuse `hub::HttpServer`. It is coupled to `Discovery`/`StreamManager`, built for SSE streaming (935 lines), and does not fit a single `GET /metrics` text endpoint.
- *Alternative rejected:* render lazily on an atomic "render requested" flag. Adds a wake latency and a second synchronization point for no benefit at scrape frequencies.

### D6 — Prometheus rendering

Mapping (client already computed everything; the daemon only renders):

| `MetricKind` | Exposition |
|---|---|
| `Counter` | `# TYPE <n> counter` + `<n>{labels} <value>` |
| `Gauge` | `# TYPE <n> gauge` + `<n>{labels} <value>` |
| `DescriptiveStatistics` | `<n>_mean`/`_stddev`/`_min`/`_max`/`_count` gauges |
| `ExplicitHistogram`/`ExponentialHistogram` | `# TYPE <n> histogram` + cumulative `_bucket{le=...}` + `_sum`/`_count` |
| `State` | `# TYPE <n> gauge` + `<n>{state="..."} 1` |

Labels are `app` (frame `app_id`), `meter` (identity `meter_name`), `version` (identity `meter_version`), `pid` (frame `pid`). `unit` is emitted as a `# UNIT <name> <unit>` comment, not a label. No per-sample timestamps (Prometheus stamps the scrape). Histogram `buckets_count[i]` are per-bucket; the daemon accumulates a running total and emits `le` = the bucket's upper edge, with the overflow bucket as `le="+Inf"`.

### D7 — JSON-lines history with quota

The daemon accumulates a window's frames, then serializes one JSON line per window with the existing `to_json(MetricBatchSnapshot)` machinery (reusing `MetricRecord`/`MetricBatchSnapshot` serialization), appending to the configured storage file. The line carries `stamp_ns` (the window's monotonic stamp), `utc_ns` (the same instant converted with the UTC offset sampled at startup), `record_count`, and the `records` array. A fixed `max_storage_mb` quota rotates or discards the oldest file once exceeded. `SIGTERM` triggers drain + `fsync`.

### D8 — Configuration

`MetricDaemonConfig` carries `service_name`, `bind_address`, `port`, `endpoint` (default `/metrics`), `enable_http`, `storage_directory`, `max_storage_mb`, `batch_flush_interval_ms`, `max_metrics`, and `bg_cpu_affinity` (a `core::ThreadSpec`). Validation rejects an empty service name, a non-absolute storage directory, and a bind address with an empty host.

## Risks / Trade-offs

- **~4.4 KB frame per metric per window** → For a process with many histograms this is a large shared-memory volume. Mitigation: publish at `collect_interval` (not per observation), and note histogram-bucket compaction as a future optimization; the MVP keeps the full snapshot for correctness.
- **`pid` label churns time series on process restart** → Accepted (explicit user decision); the JSON-lines history is the authoritative record for cross-restart correlation.
- **Double-buffered exposition can lag a scrape by up to one batch interval** → Acceptable for Prometheus; the interval is the existing batch-flush timer.
- **Single shared service means all producers contend on one publisher list** → Matches the logging pipeline; `max_subscribers` and iceoryx2's discard-on-full behavior bound the blast radius.

## Migration Plan

Purely additive: no existing capability changes, no data migration. Rollback is removing the new sink/daemon targets.

## Open Questions

None — the scope, wire format, labels, and rendering were resolved before this design.
