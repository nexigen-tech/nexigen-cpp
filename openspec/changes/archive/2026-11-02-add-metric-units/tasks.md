# Tasks

## 1. Add the unit to the metric identity

- [x] 1.1 Add `char unit[max_name_length + 1]{}` to `MetricIdentity` in `include/nexigen/metrics/metric_snapshot.hpp`; verify the identity stays trivially copyable
- [x] 1.2 Add `unit` to `MetricIdentity` `to_json`/`from_json` in `src/metrics/snapshot_format.cpp`; verify a unit round-trips and an empty unit stays empty

## 2. Thread the unit through creation

- [x] 2.1 Add a `std::string_view unit = {}` parameter to the six `Meter::create_*` declarations in `include/nexigen/metrics/meter.hpp` and their definitions in `metric_provider.hpp`, forwarding it to `create_instrument` which copies it into the identity; verify an instrument created with a unit harvests that unit and one without harvests empty

## 3. Serialize and render the unit

- [x] 3.1 Append `,"unit":"<escaped>"` in `JsonFileMetricSink::on_batch` only when the unit is non-empty; verify a unit appears in the JSONL and an empty unit is omitted
- [x] 3.2 Append ` <unit>` to the value in the hub metric formatter (`src/hub/web_assets.cpp`) when `record.unit` is present; verify a unit renders with the value

## 4. Verify

- [x] 4.1 Extend the metrics tests for unit round-trip, sink serialization, and creation; verify `nexigen_metrics_tests` passes
- [x] 4.2 Build and run the metrics and hub test suites plus `hub_cli_test.sh`; verify 100% pass
- [x] 4.3 Build and run the build-mode matrix — Debug, Release (`-O3 -march=native -flto`), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) — running the metrics and hub tests; verify no sanitizer report

## Workflow follow-up

- Explore a full operator dashboard (live cards / charts) for the hub.
