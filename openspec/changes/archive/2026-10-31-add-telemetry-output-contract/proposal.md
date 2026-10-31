# Proposal

## Why

Every nexigen application emits four JSONL streams, but only logging can be redirected through settings: metrics, tracing, and chrono sink paths are hard-coded in each application's policy, and there is no shared environment contract for run/instance identity. A local observatory cannot isolate multiple app instances, discover their files, or attribute records without per-app custom code. Fixing this now, before the lab applications exist, gives every nexigen language one deployment contract instead of a later format retrofit.

## What Changes

- Add an optional `sink` block to `metrics.json`, `tracing.json`, and `chrono.json` settings, carrying the existing file-sink configuration (path, append, rotation) with the same best-effort diagnostics and defaults → global → local → environment precedence as every other field.
- Expose that sink configuration through `app::Metrics_settings`, `app::Tracing_settings`, and `chrono::Chrono_settings` so policy hooks can construct `JsonFileMetricSink`, `JsonFileTraceSink`, and `JsonFileChronoSink` without hard-coded paths.
- Add a run/instance environment contract: `NEXIGEN_RUN_ID`, `NEXIGEN_RUN_DIR`, `NEXIGEN_APP_NAME`, `NEXIGEN_INSTANCE_ID`, and `NEXIGEN_INSTANCE_DIR`.
- Add a cold-path `nexigen::app::Run_identity` value, an environment loader, and a telemetry-path resolver with this precedence: explicit settings path → `<instance_dir>/<metrics|traces|chrono>.jsonl` → empty (sink not configured by the helper).
- Extend `Metrics_settings`, `Tracing_settings`, `Chrono_settings`, and `App_settings` serialization to include the sink configuration, and add the missing serialization trio for the new identity/path value types.
- Logging keeps its existing `log_root_directory` / `app_name` settings; `nexigen-run` is expected to set `log_root_directory` to the instance directory in the generated logging settings.
- No hot-path change, no new dependency, no record-layout change, and no network transport. All work is initialization-time and cold path only.

## Capabilities

### New Capabilities

- `app/telemetry-layout`: run/instance identity from the environment and resolution of metrics/tracing/chrono sink paths for a managed or observed application instance.

### Modified Capabilities

- `app/settings`: metrics and tracing settings gain a file-sink configuration block, serialization, and merge/diagnostic behavior for its fields.
- `chrono/chrono-settings`: chrono settings gain a file-sink configuration block, serialization, and merge/diagnostic behavior for its fields.

## Impact

- `include/nexigen/app/app_settings.hpp`, `src/app/app_settings.cpp`
- new `include/nexigen/app/telemetry_layout.hpp` + `src/app/telemetry_layout.cpp`
- `include/nexigen/chrono/chrono_settings.hpp`, `src/chrono/chrono_settings.cpp`
- settings loader tests (`tests/app/`, `tests/chrono/`), settings examples (`examples/app/`), and the README/docs sections that describe settings keys.
- No new external dependency. The existing metrics/tracing/chrono sink configuration types are reused inside the settings values.
- Performance: zero impact on producer, tick, snapshot, and sink hot paths. The new parsing, path resolution, and string allocation run only before workers start; no atomics, locks, or allocations are added to steady state.
- Follow-up (separate change, not implemented here): `nexigen-observatory` adds the hybrid `nexigen-hub` (observer/ingest/UI) and `nexigen-run` (instance layout, `resource.json`, stdio capture, `exit.json`) that consume this contract.
