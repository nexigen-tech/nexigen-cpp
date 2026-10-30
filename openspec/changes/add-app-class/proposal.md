# Proposal

## Why

Every `nexigen` application currently re-implements the same bootstrap: load logging settings, construct non-movable loggers, build metric/trace sinks, construct providers in a lifetime-safe order, handle SIGINT, flush everything on exit, and log why it stopped. Ordering mistakes are expensive (a sink destroyed before its provider, or a signal handler logging before the logger exists). A shared base class makes the correct lifecycle the default and gives HIL applications a predictable shutdown path.

## What Changes

- Add a new module `nexigen::app` (`include/nexigen/app/`, `src/app/`, `tests/app/`, target `nexigen::app`).
- Add `App<Derived>` — a CRTP base that owns the infrastructure lifecycle:
  - the derived application declares its logger/sink types and members (they are non-movable and must outlive the providers);
  - `initialize(const App_settings&)` configures the logger, constructs the metric and tracing providers around the derived sinks, initializes the tracer, and installs signal handling;
  - exposes the infrastructure to application code: `logger()`, `metrics()` + `meter(name, version)`, and `tracer()` + `tracer_for(scope)` (factory conveniences return `NotInitialized` before initialization);
  - `run(Tick&&)` runs a user tick callback until a stop is requested and returns the `App_stop_reason`;
  - `shutdown()` is idempotent, stops the signal watcher, flushes tracer → metrics → logger, restores the signal mask, and clears the `current()` pointer;
  - the destructor calls `shutdown()` if initialization succeeded.
- Add `App_stop_reason` (`None`, `Requested`, `Sigint`, `Sigterm`) with the full enum contract (`to_string`/`from_string`, `to_json`/`from_json`, `std::formatter`).
- Add signal handling: block `SIGINT`/`SIGTERM` process-wide before any worker starts, run a dedicated watcher thread on a `signalfd`, log the termination reason through the application logger, then request the stop with the matching reason.
- Add a non-owning `App<Derived>::current()` pointer, published by `initialize()` and cleared by `shutdown()`, for logging/metrics access from deep code without a true process singleton.
- Add `nexigen::app` settings loaders analogous to `logging::load_logging_settings`:
  - `MetricProvider_config` and `TracerProvider_config` are loaded from **separate** global/local JSON pairs;
  - same precedence (defaults → global → local → environment) and best-effort diagnostics;
  - `App_settings` aggregates the logging, metrics, and tracing settings, and `load_app_settings(paths, out)` loads all three pairs.
- Add a runnable `ex_app_lifecycle` example: loads temporary settings pairs, starts the App, logs through `logger()`, creates and records a metric through `meter()`, opens a span through `tracer_for()`, raises `SIGINT` from a helper thread, and verifies that the shutdown reason, metric, and span output were produced.
- Update the `examples` capability catalogue and README for the new executable.

**BREAKING**: none. All new code; existing loggers/providers keep their APIs.

## Capabilities

### New Capabilities

- `app/application`: lifecycle, component ownership/ordering, `App_stop_reason`, SIGINT/SIGTERM handling with shutdown logging, `current()` accessor, and the runnable lifecycle example.
- `app/settings`: separate global/local settings pairs for metrics and tracing, merge precedence, environment overrides, best-effort diagnostics, and the combined `App_settings` loader.

### Modified Capabilities

- `examples`: adds `ex_app_lifecycle` to the catalogue and README invocation requirements (fifteen executables total).

## Impact

- **New code:** `include/nexigen/app/app.hpp`, `app_stop_reason.hpp`, `app_settings.hpp`; `src/app/app_settings.cpp` (and cold-path formatting); `examples/app/ex_app_lifecycle.cpp`; `tests/app/app_test.cpp`.
- **Build:** new `nexigen_app` STATIC target linking `nexigen_logging`, `nexigen_metrics`, `nexigen_tracing`, and `nexigen_chrono`/`nexigen_core`; new `examples/app/` and `tests/app/` directories; CMake module map update.
- **Dependencies:** no new third-party dependency; uses POSIX `signalfd`/`pthread_sigmask` on Linux, `nlohmann::json` cold-path only.
- **Performance:** no hot-path changes. Initialization and shutdown are cold path; the signal watcher is a dedicated sleeping thread that does nothing until a signal arrives. `run()` adds one acquire load per tick (`stop_requested()`), no allocation, no lock. Settings loaders may allocate (`std::string`, JSON, diagnostics) only during initialization.
