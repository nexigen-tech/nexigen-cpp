# Proposal

## Why

`nexigen::app::App` currently forces every application into CRTP inheritance, exposes a process-global `current()` pointer, and still allows a 10.5 MB object to be placed on the stack. The inheritance contract also hides infrastructure behind a base class, making the dependency implicit and testing awkward. The project needs infrastructure as a service that applications **own and use**, not a class they **are**; and a second initialized instance must not silently fight over `SIGINT`/`SIGTERM`.

## What Changes

**BREAKING** — the `App<Derived, ...>` API is removed.

- Add `nexigen::app::Infrastructure<Policy, Tracer_capacity, Tracer_threads>` — a **heap-only composition service** that owns the logger, metric/tracing sinks and providers, the signalfd watcher, and the stop state.
- Add a stateless `Policy` contract: nested `Logger` / `Metrics_sink` / `Trace_sink` types plus static `emplace_logger` / `emplace_metrics_sink` / `emplace_trace_sink` methods. No inheritance, no CRTP hooks on the application.
- Add `make_infrastructure<Policy, ...>() -> core::Result<std::unique_ptr<Infrastructure<...>>>`. The constructor is private; the factory uses `new (std::nothrow)` so allocation failure is `SystemError`, not `std::terminate`. Stack/static `Infrastructure` objects cannot be constructed.
- Add a process-global signal-ownership guard: `initialize` claims ownership atomically before masking signals; a second initialized `Infrastructure` (any Policy) receives the new `ErrorCode::Conflict`; `shutdown` and rollback release it.
- Add `core::ErrorCode::Conflict` (append-only, value 7) for "operation conflicts with current process-global ownership/state".
- Remove `App::current()`; applications hold `std::unique_ptr<Infrastructure>` and pass references explicitly.
- Rename `App_stop_reason` to `Infrastructure_stop_reason` (same values/serialization).
- Keep `App_settings`, `load_app_settings`, SIGINT/SIGTERM behavior, the run loop, flush order and idempotent shutdown semantics.
- Rewrite `ex_app_lifecycle` on `Infrastructure` + `Policy`; rewrite `tests/app`; update README/architecture.

## Capabilities

### New Capabilities
None.

### Modified Capabilities

- `core/result`: adds `ErrorCode::Conflict` to the catalogue (append-only value 7) with naming/JSON/formatter coverage.
- `app/application`: replaces the CRTP/`App` and `current()` requirements with the heap-only `Infrastructure` composition contract, stateless `Policy`, `make_infrastructure`, process-global signal guard, `Infrastructure_stop_reason`, and the updated example.

## Impact

- **Code:** `include/nexigen/core/result.hpp` (enum + naming), `include/nexigen/app/app.hpp` (replaced by infrastructure API), `app_stop_reason.hpp` (renamed type), `tests/core/core_test.cpp`, `tests/app/app_test.cpp`, `examples/app/ex_app_lifecycle.cpp`, README, architecture.
- **API:** breaking removal of `App<Derived,...>` and `App::current()`; `App_stop_reason` renamed. ~110 references to update.
- **Memory:** `Infrastructure` is ~10.5 MB with default tracer settings and is now allocated once at startup on the heap. `make_infrastructure` performs exactly one cold allocation; no allocations on `run`/recording paths.
- **Performance/latency:** no hot-path change. `run` still performs one acquire load per tick. The guard adds one CAS per `initialize`/`shutdown` and one atomic load per second initialization attempt — cold path only.
- **Dependencies:** no new third-party dependency; uses existing `std::nothrow`, POSIX signals/signalfd.
