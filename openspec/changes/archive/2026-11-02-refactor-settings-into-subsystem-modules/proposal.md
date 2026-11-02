# Proposal

## Why

The settings types are split inconsistently across namespaces. `logging` and `chrono` each own their `Settings` type and loader, and each carries a byte-identical copy of the `SettingsSource`/`SettingsPaths`/`SettingsDiagnostic` vocabulary (chrono duplicated it only to avoid depending on logging). Meanwhile `metrics` and `tracing` have their merged settings (`app::MetricsSettings` / `app::TracingSettings`) and loaders living in `nexigen::app`, so the app layer owns subsystem configuration semantics instead of composing them. On top of that, the settings specs still spell type names in snake_case (`App_settings`, `Metrics_settings`, `Settings_diagnostic`) while the code uses PascalCase per mandate 8. This change consolidates the shared vocabulary into `core`, moves each subsystem's settings and loader into its own module, and aligns spec naming with the code.

## What Changes

- Move `SettingsSource`, `SettingsPaths`, and `SettingsDiagnostic` into `nexigen::core` as one shared vocabulary. `logging` and `chrono` switch to `core::` and drop their local (duplicated) definitions. **BREAKING** — the types move from `nexigen::logging` / `nexigen::chrono` to `nexigen::core`.
- Move `app::MetricsSettings` + `load_metrics_settings` into `nexigen::metrics` as `metrics::Settings` + `metrics::load_metrics_settings`. **BREAKING** — namespace move.
- Move `app::TracingSettings` + `load_tracing_settings` into `nexigen::tracing` as `tracing::Settings` + `tracing::load_tracing_settings`. **BREAKING** — namespace move.
- Rename `app::SettingsPaths` (the aggregate of four path pairs) to `app::AppSettingsPaths`, so it no longer collides with `core::SettingsPaths` (a single global/local pair). `app::Settings` (the aggregate of four subsystem `Settings`) is kept unchanged. **BREAKING** — `app::SettingsPaths` renamed.
- Make `app::load_app_settings` symmetric: it now delegates to the four subsystem loaders (`logging::`, `chrono::`, `metrics::`, `tracing::`) and aggregates the worst status, instead of owning metrics/tracing loading itself.
- Align settings specs to PascalCase type names (mandate 8), matching the code.

## Capabilities

### New Capabilities

- `core/settings-vocabulary`: the shared `SettingsSource` enum and `SettingsPaths` / `SettingsDiagnostic` structs used by every settings loader, including their `to_string`, `std::formatter`, and non-throwing `to_json`/`from_json` contract.
- `metrics/metrics-settings`: the metrics settings loader (`metrics::Settings`, `load_metrics_settings`) — global/local JSON pairs, precedence, environment overrides, sink block, and best-effort diagnostics — relocated out of `app/settings`.
- `tracing/tracing-settings`: the tracing settings loader (`tracing::Settings`, `load_tracing_settings`) — same shape as metrics — relocated out of `app/settings`.

### Modified Capabilities

- `app/settings`: metrics and tracing settings requirements move to their own modules; `SettingsPaths` renamed to `AppSettingsPaths`; `App_settings` → `Settings`; snake_case names corrected to PascalCase.
- `logging/logging-settings`: `SettingsSource`/`SettingsPaths`/`SettingsDiagnostic` now come from `core::`; `Logging_settings` → `Settings`; snake_case names corrected.
- `chrono/chrono-settings`: `Chrono_settings_paths`/`Chrono_settings_diagnostic` now come from `core::`; `Chrono_settings` → `Settings`; snake_case names corrected.
- `app/command-line`: `App_settings_paths` → `AppSettingsPaths`, `App_settings` → `Settings` (naming only).
- `app/application`: `App_settings` → `Settings`, `App_settings_paths` → `AppSettingsPaths` (naming only).

## Impact

- **Code**: new core header for the vocabulary; new `include/nexigen/metrics/metrics_settings.hpp` and `include/nexigen/tracing/tracing_settings.hpp` (+ src bodies relocated from `src/app/app_settings.cpp`); edits to `include/nexigen/logging/logging_settings.hpp`, `include/nexigen/chrono/chrono_settings.hpp`, and the app headers `app_settings.hpp`, `command_line.hpp`, `infrastructure.hpp`, `telemetry_layout.hpp`; plus their `src/` bodies.
- **Tests / examples**: `tests/logging/logging_test.cpp`, `tests/chrono/chrono_test.cpp`, `tests/app/app_test.cpp`, and the settings examples (`ex_app_cli_settings`, `ex_suite_app`, `ex_app_lifecycle`) reference the moved/renamed types and must be updated.
- **Performance / latency / allocation**: none. This is a cold-path-only reorganization; no hot-path type, producer, tick, snapshot, or sink path is touched, and steady-state zero-allocation and wait-free guarantees are unaffected.
