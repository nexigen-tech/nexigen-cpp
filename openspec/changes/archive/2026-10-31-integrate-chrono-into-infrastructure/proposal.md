# Proposal

## Why

The `nexigen::chrono` clock service (`ChronoProvider`, sinks, settings) was added as a standalone subsystem but is not wired into `Infrastructure`, so applications cannot obtain a snapshot service through the same composition path they use for logging, metrics, and tracing. This change integrates it, giving the clock service a two-phase start so its background worker can log through the application logger.

## What Changes

- Extend `ChronoProvider` with a two-phase lifecycle: construction pre-allocates and provides `snapshot_now()` without starting the worker, and a new `start()` (with an optional logger) starts the background snapshot worker. **BREAKING**: the worker no longer auto-starts in the constructor.
- Extend `InfrastructurePolicy` to require a `Chrono_sink` type and a static `emplace_chrono_sink` hook, alongside the existing logger/metrics/tracing members. **BREAKING**: every existing policy must add these two declarations.
- Extend `Infrastructure` to own a `Chrono_sink` and a `chrono::ChronoProvider`, construct the provider first (deferred worker), then create the logger, bind the logger to the chrono provider and start its worker, then construct metrics and tracing. Shutdown delivers a final snapshot and flushes chrono; a `chrono()` accessor is added.
- Extend `App_settings` with a `chrono` member (reusing `chrono::Chrono_settings` directly, like logging reuses `logging::Logging_settings`) and `App_settings_paths` with `chrono::Chrono_settings_paths`; `load_app_settings` loads chrono via `chrono::load_chrono_settings`.

## Capabilities

### New Capabilities

<!-- None: the clock service capabilities already exist in the main spec tree.
     This change integrates them into app and adds the two-phase start. -->

### Modified Capabilities

- `chrono/chrono-provider`: `ChronoProvider` gains a deferred worker start (`start()`) and an optional logger binding for the background worker; construction no longer auto-starts the worker.
- `app/application`: `Infrastructure` gains a `Chrono_sink` + `ChronoProvider`, the policy contract gains `Chrono_sink`/`emplace_chrono_sink`, initialization/shutdown/accessors gain the chrono provider (constructed first, worker started after the logger), and the minimal example exercises chrono.
- `app/settings`: `App_settings`/`App_settings_paths` gain a chrono member, `load_app_settings` loads chrono settings, and their serialization surfaces cover chrono.

## Impact

- Files: `include/nexigen/chrono/chrono_provider.hpp`, `src/chrono/chrono_provider.cpp`, `include/nexigen/app/infrastructure.hpp`, `include/nexigen/app/app_settings.hpp`, `src/app/app_settings.cpp`, `examples/app/ex_app_lifecycle.cpp`, `tests/chrono/chrono_test.cpp`, `tests/app/app_test.cpp`, `examples/chrono/ex_chrono_snapshot.cpp`.
- Performance/latency: no change to the tick/producer hot path. The `ChronoProvider` is constructed during `initialize` (cold path); its worker starts only after the logger exists. The default `App_settings{}` carries `collect_interval == 0`, so `start()` starts no background thread by default.
- Memory allocation: no steady-state allocation added; the chrono provider pre-allocates nothing beyond its fixed members (worker, mutex, atomics, log hook), matching the existing provider/sink skeleton.
- Compatibility: **BREAKING** for the `InfrastructurePolicy` contract (policy must declare `Chrono_sink` + `emplace_chrono_sink`) and for `ChronoProvider` (constructor no longer auto-starts the worker; callers must call `start()`).
