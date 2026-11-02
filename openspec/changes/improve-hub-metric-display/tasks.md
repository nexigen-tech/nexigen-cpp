# Tasks

## 1. Fix state serialization in the sink

- [x] 1.1 Add a `case MetricKind::State` branch to `JsonFileMetricSink::on_batch` in `include/nexigen/metrics/json_file_metric_sink.hpp` that writes `"value":"<escaped state>"`; verify a state record emits `"kind":"State"` and a `"value"` string, not histogram fields
- [x] 1.2 Extend `test_sink_kind_matrix` in `tests/metrics/metrics_test.cpp` with a state record and assert its `"value"`; verify the metrics test suite passes

## 2. Structured metric rendering in the hub

- [x] 2.1 Add a kind-aware formatter to the embedded `app.js` (`src/hub/web_assets.cpp`) that parses a metrics batch and renders each record per kind (counter/gauge, statistics, histogram with bucket distribution, state); verify a sample batch renders one compact line per metric
- [x] 2.2 Route the SSE `record` handler through the formatter when the selected stream kind is `metrics`, keeping other streams and unparseable lines as raw text; verify non-metrics streams still show raw JSON

## 3. Verify

- [x] 3.1 Add a `web_assets` assertion in `tests/hub/hub_test.cpp` that the embedded script contains the metric formatter (e.g. the kind dispatch); verify `nexigen_hub_tests` passes
- [x] 3.2 Build and run the metrics and hub test suites plus `hub_cli_test.sh`; verify 100% pass
- [x] 3.3 Build and run the build-mode matrix — Debug, Release (`-O3 -march=native -flto`), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) — running the metrics and hub tests; verify no sanitizer report
- [x] 3.4 End-to-end: start the suite demo, open the hub, select a metrics stream, and confirm counters/gauges/statistics/histograms/state render compactly rather than as raw JSON

## Workflow follow-up

- Add dimension/units to metrics (a `MetricRecord`/settings change in the library, not the hub).
- Explore a full operator dashboard (live cards / charts) for the hub.
