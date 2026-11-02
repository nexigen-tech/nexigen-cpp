# Tasks

## 1. Shared vocabulary in core

- [x] 1.1 Add `include/nexigen/core/settings_vocabulary.hpp` with `nexigen::core::SettingsSource` (enum), `SettingsPaths` (struct), and `SettingsDiagnostic` (struct), including their `to_string`/`from_string`, `std::formatter` specializations, and non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. Verify: the header compiles standalone under `-std=c++23 -fno-exceptions -fno-rtti -Werror`.
- [x] 1.2 Add unit coverage in `tests/core/core_test.cpp` for the vocabulary: enumerator text/JSON round-trips, hostile-JSON left-unchanged, and paths/diagnostic round-trips. Verify: `tests/core` passes under the Debug build.

## 2. Logging and chrono switch to core

- [x] 2.1 In `include/nexigen/logging/logging_settings.hpp` and `src/logging/logging_settings.cpp`, remove the local `SettingsSource`/`SettingsPaths`/`SettingsDiagnostic` definitions (plus their formatters and JSON declarations) and reference `core::` equivalents. Verify: `tests/logging` passes unchanged.
- [x] 2.2 In `include/nexigen/chrono/chrono_settings.hpp` and `src/chrono/chrono_settings.cpp`, do the same switch to `core::`. Verify: `tests/chrono` passes unchanged.

## 3. Metrics and tracing settings modules

- [x] 3.1 Add `include/nexigen/metrics/metrics_settings.hpp` and `src/metrics/metrics_settings.cpp` with `metrics::Settings` and `metrics::load_metrics_settings`, moving the loader body out of `src/app/app_settings.cpp`; wire the new source into the `nexigen_metrics` CMake target. Verify: metrics settings load/merge/sink behavior still passes its tests.
- [x] 3.2 Add `include/nexigen/tracing/tracing_settings.hpp` and `src/tracing/tracing_settings.cpp` with `tracing::Settings` and `tracing::load_tracing_settings`, moving the loader body out of `src/app/app_settings.cpp`; wire into `nexigen_tracing`. Verify: tracing settings tests pass.

## 4. App aggregator and call sites

- [x] 4.1 Rewrite `include/nexigen/app/app_settings.hpp` to the aggregator shape: `AppSettingsPaths` (four `core::SettingsPaths`) and `Settings` (four module `Settings`), with `load_app_settings` delegating to the four subsystem loaders; delete the now-relocated `load_metrics_settings`/`load_tracing_settings` declarations and their bodies in `src/app/app_settings.cpp`. Verify: `tests/app` settings tests pass.
- [x] 4.2 Update `include/nexigen/app/command_line.hpp` (`make_app_settings_paths` → `AppSettingsPaths`), `include/nexigen/app/infrastructure.hpp`, and `include/nexigen/app/telemetry_layout.hpp` to the new type names. Verify: `tests/app` passes and the app/command-line/infrastructure tests are green.

## 5. Tests, examples, and spec naming

- [x] 5.1 Update `tests/logging/logging_test.cpp`, `tests/chrono/chrono_test.cpp`, and `tests/app/app_test.cpp` for the moved/renamed types, and fix `to_string` exact-string assertions (`"TracingSettings(...)"`/`"MetricsSettings(...)"` → `"Settings(...)"`). Verify: all three suites pass.
- [x] 5.2 Update the settings examples (`examples/app/ex_app_cli_settings.cpp`, `examples/suite/ex_suite_app.cpp`, `examples/app/ex_app_lifecycle.cpp`) to the new names. Verify: the examples build and run.
- [x] 5.3 Update the stale `## Purpose` lines in the main specs `openspec/specs/app/settings/spec.md` (`App_settings` → `Settings`) and `openspec/specs/chrono/chrono-settings/spec.md` (`ChronoProvider_config` → `ChronoProviderConfig`) to match the code. Verify: `openspec validate --strict` reports no drift for these specs.

## 6. Full verification

- [x] 6.1 Compile and pass 100% of unit tests under Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify: every suite is green in all four configurations and no new warnings under `-Werror`.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived specs (including the new `core/settings-vocabulary`, `metrics/metrics-settings`, and `tracing/tracing-settings` capabilities) landed correctly.
