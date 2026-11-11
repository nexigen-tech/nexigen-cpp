# Tasks

## 1. MetricFrame wire type

- [x] 1.1 Create `include/nexigen/metrics/metric_frame.hpp`: `MetricFrame` POD with `char app_id[4]`, `std::uint32_t pid`, `std::int64_t timestamp_ns`, and a `MetricRecord record`; add `static_assert(std::is_trivially_copyable_v<MetricFrame>)`, member/free `to_string`, `std::formatter`, and `to_json`/`from_json` delegating to `MetricRecord`. Verify: `sizeof` is fixed and matches the layout in design D1, JSON round-trip preserves app_id/pid/timestamp/record, and formatter output equals member `to_string` (new test in `tests/metrics/metric_frame_test.cpp`).

## 2. Iceoryx2MetricSink

- [x] 2.1 Create `include/nexigen/metrics/iceoryx2_metric_sink_config.hpp`: `Iceoryx2MetricSinkConfig` with `app_id`, `service_name`, and `subscriber_max_buffer_size`, plus member/free `to_string`, `std::formatter`, and `to_json`/`from_json`. Verify: JSON round-trip and `to_string`/formatter equality in `tests/metrics/iceoryx2_metric_sink_config_test.cpp`.
- [x] 2.2 Create `include/nexigen/metrics/iceoryx2_metric_sink.hpp` (and `src/metrics/iceoryx2_metric_sink.cpp` if a non-inline TU is warranted): `Iceoryx2MetricSink` satisfying `concepts::MetricSink` with `start()` (open pub-sub + event service, cache `::getpid()`, apply the raised `subscriber_max_buffer_size`), `on_batch()` (fan out one `MetricFrame` per record + one notify per batch, discard-on-full), `flush()` (no-op), `stop()`, and diagnostics. Verify: `concepts::MetricSink<Iceoryx2MetricSink>` is `true`, and the publish test observes N frames + one notify (including an 8-frame burst with no drops).

## 3. Prometheus text rendering

- [x] 3.1 Create `include/nexigen/metrics/detail/prometheus_render.hpp` (and `.cpp`): render a `MetricRecord` into the exposition text with `# TYPE`/`# UNIT` comments, `app`/`meter`/`version`/`pid` labels, the `state` label for `State`, cumulative histogram `_bucket{le=...}`/`_sum`/`_count`, and no per-sample timestamps. Verify: a unit test per `MetricKind` asserts the exact expected lines for counter, gauge, descriptive statistics (mean/stddev/min/max/count), explicit and exponential histograms (cumulative buckets ending `+Inf`), and state.

## 4. MetricDaemonConfig

- [x] 4.1 Create `include/nexigen/metrics/metric_daemon_config.hpp`: `MetricDaemonConfig` with `service_name`, `bind_address`, `port`, `endpoint`, `enable_http`, `storage_directory`, `max_storage_mb`, `batch_flush_interval_ms`, `max_metrics`, and `bg_cpu_affinity` (`core::ThreadSpec`), plus member/free `to_string`, `std::formatter`, and `to_json`/`from_json`. Verify: JSON round-trip and validation rejection (empty service name, non-absolute storage directory, empty host) in `tests/metrics/metric_daemon_config_test.cpp`.

## 5. MetricsDaemon tool

- [x] 5.1 Create `tools/nexigen-metricsd/main.cpp`: `Subscriber<MetricFrame>` + `EventListener` + `WaitSet` drain; a pre-allocated open-addressing aggregator keyed by `(app, meter_name, metric_name)` (latest-wins); double-buffered `alignas(64)` exposition buffers published with `release`/`acquire`; JSON-lines window history with `max_storage_mb` quota; `SIGTERM` drain + `fsync`. Verify: builds as `nexigen-metricsd`, `--help` prints usage, and the smoke test starts it, creates the history file, and stops it on SIGTERM.
- [x] 5.2 Implement the minimal HTTP responder (epoll + `hub/detail/http_parse.hpp`): `GET <endpoint>` returns `200` with `text/plain; version=0.0.4`, any other path returns `404`, run on a `std::jthread` reading the published buffer. Verify: an HTTP client test asserts status, content type, and body for `/metrics` and `404` for an unknown path.
- [x] 5.3 Register the `nexigen-metricsd` target in CMake behind `NEXIGEN_ENABLE_COM` (links the `com` module and `nexigen_metrics`). Verify: the binary exists when the gate is ON and is absent when OFF.

## 6. Tests

- [x] 6.1 End-to-end: `Iceoryx2MetricSink` publishes frames from a `MetricProvider` → the daemon pipeline subscribes → scraping `/metrics` returns the aggregated exposition, and the storage directory gains a JSON line whose timestamp and records match. Verify: `tests/metrics/metrics_daemon_test.cpp` passes.
- [x] 6.2 Aggregation latest-wins: two frames for the same `(app, meter, metric)` yield one time series carrying the later record, and distinct metrics are kept separately. Verify: `tests/metrics/daemon_components_test.cpp` passes.
- [x] 6.3 Shutdown flush: sending `SIGTERM` (or a stop request) flushes and `fsync`s buffered history before exit. Verify: the smoke test stops the daemon on SIGTERM and the history writer flush/rotation test confirms appended windows persist.

## 7. Verification matrix

- [x] 7.1 Debug gate-ON: full `ctest` green.
- [x] 7.2 Release `-O3 -march=native -flto` + `NEXIGEN_STRICT_LATENCY_TESTS=ON`: full `ctest` green.
- [x] 7.3 ThreadSanitizer `-DNEXIGEN_ENABLE_TSAN=ON`: full `ctest` green.
- [x] 7.4 AddressSanitizer `-DNEXIGEN_ENABLE_ASAN=ON`: full `ctest` green.

## Workflow follow-up

- OS telemetry (`/proc`/cgroups/thermal), PHM threshold alerting, and iceoryx2 summary re-publish — separate additive changes.
- Archive the change after the project's review requirements are satisfied.
