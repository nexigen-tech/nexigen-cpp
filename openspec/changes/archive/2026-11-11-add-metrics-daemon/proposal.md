# Proposal

## Why

`MetricProvider` already harvests every process's metrics into a bound `MetricSink`, but all existing sinks are in-process (console, JSON-file, null). There is no central place to observe the whole platform's metrics across processes — no Prometheus scrape endpoint and no shared on-disk history. This change adds the metrics analog of the logging pipeline (`Iceoryx2Logger` → `LogDaemon`): a client-side sink that publishes harvested batches over iceoryx2, and a `MetricsDaemon` that subscribes, aggregates, and exports them. The math (mean/stddev, histogram percentiles) and reset semantics stay on the client — the daemon only renders and stores what the client already computed.

## What Changes

- New `Iceoryx2MetricSink` satisfying `concepts::MetricSink`: on `on_batch` it wraps each harvested `MetricRecord` in a fixed, trivially-copyable `MetricFrame` (adding the application identity, the publishing PID, and the window timestamp), then publishes each frame over an iceoryx2 pub-sub service and notifies once per window via `EventNotifier`. `flush()` is a no-op because the data is already durable in shared memory at publish time.
- New `MetricFrame` POD wire type, shared by the sink and the daemon (the cross-process contract, mirroring `LogFrame`).
- New tool `tools/nexigen-metricsd`: `Subscriber<MetricFrame>` + `EventListener` + `WaitSet` drain loop; aggregates the latest record per `(app, meter, metric)`; serves Prometheus text exposition on `GET /metrics` through a minimal epoll HTTP responder; appends window history as JSON-lines to a quota-bounded directory.
- New `MetricDaemonConfig` (subscription service name, HTTP bind address/port/endpoint, storage directory and quota, worker thread affinity) with the full `to_string`/`std::formatter`/JSON serialization contract.
- `com::Publisher::create` / `com::Subscriber::create` gain an optional `subscriber_max_buffer_size` so the daemon's subscriber can hold a full publish burst (one frame per record) without deterministic discards.
- **BREAKING**: none — purely additive. The producer hot path is untouched; the new sink runs on the provider's existing background harvester thread.

## Capabilities

### New Capabilities

- `metrics/iceoryx2-metric-sink`: the client-side sink that publishes harvested metric batches to iceoryx2 for central collection, including the `MetricFrame` wire type.
- `metrics/metrics-daemon`: the central metrics daemon that subscribes to frames, aggregates them, and exports Prometheus text plus a quota-bounded JSON-lines disk history.

### Modified Capabilities

- `com/publish-subscribe`: `Publisher::create` / `Subscriber::create` gain an optional `subscriber_max_buffer_size`, so a subscriber can hold a full metric publish burst (one frame per record) instead of the iceoryx2 default of two samples.

## Impact

- **Code**: new headers under `include/nexigen/metrics/` (sink, frame, daemon config, Prometheus renderer), new `tools/nexigen-metricsd/`, new tests under `tests/metrics/`. Reuses `hub/detail/http_parse.hpp` for HTTP request parsing.
- **Performance / latency / memory**: the producer hot path is unchanged — instrumentation still records with no locks or allocation. Publishing happens on the provider's existing harvester `std::jthread`, never on a real-time thread; each window copies one ~4.5 KB POD frame per record (bounded by `max_snapshot_buckets` = 256) into a zero-copy shared-memory loan, allocation-free. The daemon drains without polling (WaitSet sleep — 0% CPU when idle), runs on a service core via a `ThreadSpec` affinity, and holds the latest snapshot behind an atomic swap so the HTTP thread never blocks the drain loop.
- **Dependencies / APIs**: the sink and daemon are gated behind `NEXIGEN_ENABLE_COM` (the same gate as `nexigen-logd`), linking the `com` module and `nexigen_metrics`. No new third-party dependency is added (nlohmann/json 3.12.0 remains the only JSON library; HTTP is hand-rolled over the existing parser helpers).
