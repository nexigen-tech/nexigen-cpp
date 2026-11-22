# Proposal

## Why

A minimal application still pays roughly 70–100 lines of infrastructure boilerplate before its
actual logic: CLI and settings loading, time-base setup, `make_infrastructure`, initialization,
handle lookup, node/service/channel creation, a WaitSet loop, and shutdown. The shortest current
example, `ex_app_wait_loop_iceoryx2.cpp`, is only 14 lines because its real body moved into a
200-line example header — the abstraction exists, but it is copied into examples instead of being a
library API. Applications cannot reach the platform's target of 20–30 readable lines, and each copy
can drift in signal handling, app identity, and shutdown order.

## What Changes

- Add `app::App<Policy>`: an explicit, heap-owned application facade with `create`, `run`, and
  `shutdown`.
  - `create(argc, argv, app_name, TimeBaseMode)` parses the standard application options, loads
    settings files when directories are supplied, and otherwise uses defaults.
  - `counter`/`gauge`/`histogram` convenience methods return one-line handles.
  - `subscribe<T>(service, callback)` and `publish<T>(service)` wrap event channels and producers
    with the application's `app_id`.
  - `run()` drives one event input and an optional output port through a bounded WaitSet loop.
  - `shutdown()` is explicit and idempotent; the destructor is a safety net.
- Add `TimeBaseMode` with `External` (default) and `Embedded` (explicit examples/tests mode).
  External mode attaches to the published base and fails when it is absent; Embedded creates the
  local example/test base.
- Require a non-empty `app_id` from `--app-id` or the suite manifest. The facade does not fall back
  to the application name.
- Collect diagnostics from every declared instrument and port. `run()` SHALL refuse to start and
  return `InvalidArgument` when any of them failed, naming the failing declarations.
- Extend graceful shutdown:
  - `SIGHUP` becomes a `Sighup` stop reason handled by the existing `signalfd` watcher.
  - `SIGINT`, `SIGTERM`, and `SIGHUP` exit with code 0; `TimeBaseLost` keeps code 75.
  - `App` ignores `SIGPIPE` for its lifetime and restores the previous disposition after telemetry
    quiescence, so writes to a closed console/pipe return `EPIPE` instead of killing the process.
- Add a runnable quickstart example whose main body stays within roughly 20–30 lines, receives one
  signal, prints or transforms it, and optionally publishes the result over iceoryx2 so
  `nexigen-recd` can record both telemetry and events.

## Capabilities

### New Capabilities

- `app/runtime`: the `App<Policy>` facade, its CLI/default-settings bootstrap, time-base modes,
  one-line metric handles, one-input/optional-output event ports, diagnostic gating, signal and
  exit-code behavior, and shutdown ownership.
- `examples/app-quickstart`: a runnable minimal application that demonstrates the facade within the
  target line budget and remains recordable through iceoryx2.

### Modified Capabilities

- `app/application`: `Infrastructure` gains the `Sighup` stop reason, masks and watches `SIGHUP`
  through `signalfd`, renders it in the stop-reason vocabulary, and logs it during shutdown.

## Impact

- **Code:** new `include/nexigen/app/app.hpp` (or `runtime.hpp`) and `app_options.hpp`; a reusable
  embedded time-base header moved out of `examples/app/time_base_fixture.hpp`; changes to
  `infrastructure.hpp` and `infrastructure_stop_reason.hpp`; a new example under `examples/app/`.
- **APIs:** new `app::App<Policy>`, `TimeBaseMode`, and an exit-code helper; instruments and ports
  reuse the existing provider-owned and transport handle types. `InfrastructureStopReason` gains an append-only enumerator and one watcher
  branch. No existing API is removed.
- **Behavior:** suite-launched apps can use `App` without changing their manifest or identity;
  standalone quickstart apps use `Embedded` and must pass `--app-id`. `SIGHUP` now stops rather than
  killing the process. `SIGPIPE` is ignored only while an `App` is alive.
- **Performance:** all new work is cold path. The only steady-state change is one additional
  `signalfd` comparison per watcher wake-up; instrument/port registration and `SIGPIPE` handling
  happen during `create`. No hot-path allocation, locking, or branching is added.
- **Tests:** unit tests for lifecycle, diagnostic gating, embedded/external time base,
  SIGHUP/SIGPIPE/exit codes, and one-input dispatch; a quickstart smoke test; recorder round-trip
  for the example's telemetry and events.
- **Docs:** `docs/articles/app.md`, the stop-reason documentation, and a quickstart recipe showing
  the 20–30 line application.
