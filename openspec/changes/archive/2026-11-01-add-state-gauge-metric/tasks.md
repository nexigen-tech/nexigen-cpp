# Tasks

## 1. Vocabulary and record payload

- [x] 1.1 Add `MetricKind::State` to `policy_enums.hpp` with `to_string`/`from_string` (`"State"`), `to_json`/`from_json`, and the `std::formatter` specialization. Verify: `nexigen_metrics_tests` policy-enum scenarios (catalogue, name mapping round-trip) pass.
- [x] 1.2 Add a `StateValue` inline string member (`char value[max_state_length + 1]`) to the `MetricRecord` union in `metric_snapshot.hpp`, and extend `src/metrics/snapshot_format.cpp` so `MetricRecord::to_string`/`to_json`/`from_json` handle `MetricKind::State`. Verify: a state record round-trips through JSON.

## 2. StateGauge instrument

- [x] 2.1 Create `include/nexigen/metrics/state_gauge.hpp` with `StateGauge<Concurrency, Access, Reset>`: `set(std::string_view) noexcept -> bool` (reject empty / >63), `read() const -> std::string_view`, `snapshot() const -> MetricRecord`, `to_string`/free `to_string`/`to_json`/`std::formatter`, `harvest_thunk`, and no `from_json`. Verify: the header compiles and satisfies the `Metric`/`ReadableMetric` concepts.
- [x] 2.2 Implement `SingleThreaded` storage (plain buffer) and `MultiThreaded` storage (seqlock last-writer-wins) per design D4, with `ResetPolicy::ResetOnRead` clearing the buffer after observation. Verify: unit tests below cover both modes under ThreadSanitizer.

## 3. Registry and factory

- [x] 3.1 Add `max_states` to `MetricProviderConfig` (with `to_json`/`from_json`) and `max_state_size` to `detail/layout.hpp`. Verify: the config round-trip test passes.
- [x] 3.2 Extend `metric_provider.hpp` and `src/metrics/provider.cpp`: `kind_count` 5 → 6, `kind_index`/`kind_object_size`/`reserve_slot` support for `State`, and a `max_states` registry pool. Verify: the provider builds and the registry budget static_asserts hold.
- [x] 3.3 Add `Meter::create_state(name)` and the `MetricProvider` factory path (mirroring `create_counter`/`create_gauge`), returning `core::Result<StateGauge*>`. Verify: `create_state` returns a working gauge that records and harvests.

## 4. Tests

- [x] 4.1 Add unit tests for `StateGauge`: recording surface (valid/empty/over-long `set`), read/snapshot, `SingleThreaded` and `MultiThreaded` last-writer-wins (no torn reads under TSan), reset windows, serialization contract (write-only JSON), concept satisfaction, and factory creation. Verify: `nexigen_metrics_tests` passes.

## 5. Example

- [x] 5.1 Add a runnable `examples/metrics/` example that creates a state gauge, sets a mode/enum spelling (via `to_string(enum)`), harvests it, and prints the record. Verify: the example builds and its ctest entry passes.

## 6. Integration

- [x] 6.1 Run the full Debug build, `ctest` (33 targets), `cmake/check_naming.py`, and `cmake/check_result_value.py`. Verify: all green.

## Workflow follow-up

- Archive the change after review, then verify `openspec validate --specs` and the coverage gate.
