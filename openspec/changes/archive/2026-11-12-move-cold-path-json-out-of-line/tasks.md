# Tasks

## 1. Move the metrics serializers

- [x] 1.1 Move `MetricFrame`'s `to_json`/`from_json` from `include/nexigen/metrics/metric_frame.hpp` into `src/metrics/metric_frame.cpp`; verify the header keeps only declarations, the file builds and `metrics` round-trip/hostile tests pass.
- [x] 1.2 Move `JsonFileMetricSinkConfig`'s `to_json`/`from_json` and `JsonFileMetricSink::to_json` from `include/nexigen/metrics/json_file_metric_sink.hpp` into `src/metrics/json_file_metric_sink.cpp`; verify the sink/config tests pass.
- [x] 1.3 Move `Iceoryx2MetricSinkConfig`'s pair into `src/metrics/iceoryx2_metric_sink_config.cpp`; verify its config round-trip test passes.
- [x] 1.4 Move `ConsoleMetricSink`'s pair into `src/metrics/console_metric_sink.cpp`; verify the daemon component tests pass.

## 2. Move the chrono serializers

- [x] 2.1 Move `JsonFileSinkConfig`'s pair into `src/chrono/json_file_sink_config.cpp`; verify chrono tests pass.
- [x] 2.2 Move `ConsoleSink`'s pair into `src/chrono/console_sink.cpp`; verify chrono tests pass and the sink still round-trips its descriptor.

## 3. Verification

- [x] 3.1 Verify no moved symbol remains inline in its header (`grep -c "inline void to_json\\|inline void from_json"` is zero for the six files) and that `lizard -C 15` still passes for `include/`, `src/`, `tests/`, `tools/`.
- [x] 3.2 Compare `nm -C --defined-only --extern-only` for `libnexigen_metrics.a` and `libnexigen_chrono.a` before and after: verify every removed `.W` symbol has a matching strong `.T` definition in the library, and no public declaration disappeared.
- [x] 3.3 Build and run `ctest` in Debug (`build-com`, `build-verify`), Release, ThreadSanitizer and AddressSanitizer; verify zero failures.
- [x] 3.4 Run `check_naming.py`, `check_result_value.py`, `check_complexity.py`, `format-check` and `ctest -R complexity`; verify all green.
- [x] 3.5 Update `docs/header-compilation-audit.md` so its "what moved" and remaining-count tables reflect the six new `src/` units.
- [x] 3.6 Run `openspec validate move-cold-path-json-out-of-line --strict`.

## Workflow follow-up

- Commit the move, archive with `/opsx-archive`, and push.
