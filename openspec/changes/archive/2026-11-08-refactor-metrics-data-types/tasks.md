# Tasks

## 1. Snapshot leaf types (standalone)

- [x] 1.1 Extract `MetricIdentity` + `detail::copy_meter_identity` + `max_name_length`/`max_state_length`/`max_snapshot_buckets` constants + `std::formatter<MetricIdentity>` into `include/nexigen/metrics/metric_identity.hpp`. Verify `nexigen_metrics` compiles.
- [x] 1.2 Extract `ScalarValue` + `std::formatter<ScalarValue>` into `include/nexigen/metrics/scalar_value.hpp`. Verify.
- [x] 1.3 Extract `StatisticsSnapshot` + `std::formatter<StatisticsSnapshot>` into `include/nexigen/metrics/statistics_snapshot.hpp`. Verify.
- [x] 1.4 Extract `HistogramSnapshot` + `std::formatter<HistogramSnapshot>` into `include/nexigen/metrics/histogram_snapshot.hpp`. Verify.
- [x] 1.5 Extract `StateValue` into `include/nexigen/metrics/state_value.hpp`. Verify.

## 2. Snapshot aggregate types

- [x] 2.1 Extract `MetricRecord` + `std::formatter<MetricRecord>` into `include/nexigen/metrics/metric_record.hpp`. Includes all 5 leaf type headers. Verify.
- [x] 2.2 Extract `MetricBatchSnapshot` + `std::formatter` into `include/nexigen/metrics/metric_batch_snapshot.hpp`. Verify.

## 3. metric_snapshot forwarding

- [x] 3.1 Replace `metric_snapshot.hpp` content with a forwarding header that includes all 7 type headers. Verify all consumers compile.

## 4. metric_provider config

- [x] 4.1 Extract `MetricProviderConfig` + `std::formatter` into `include/nexigen/metrics/metric_provider_config.hpp`. Verify.
- [x] 4.2 Extract `SinkBinding` into `include/nexigen/metrics/sink_binding.hpp`. Verify.
- [x] 4.3 Trim `metric_provider.hpp` to `MetricProvider` only, including the new config/binding headers. Verify.

## 5. Verification

- [x] 5.1 Build and run `nexigen_metrics_tests` under Debug and Release+LTO. Verify all tests pass.