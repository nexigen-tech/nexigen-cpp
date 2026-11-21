# Proposal

## Why

Three runtime contracts still leave holes that the diagnostics and lifetime work did not close:

1. **The trace daemon still samples `CLOCK_REALTIME` once at startup** as a UTC fallback, even when it attaches to the published time base. ADR 0001 allows the raw offset only for contexts without a base; the daemon should require the base instead of carrying a private wall-clock sample.
2. **`Infrastructure::logger()` and `metrics()` dereference an empty `optional` before initialization.** Unlike `meter()`/`tracer_for()`, they have no failure channel, so a pre-init call is undefined behavior rather than `NotInitialized`.
3. **`bind_diagnostics` can be called on a running provider.** The 16-byte binding is read by the worker, so a live rebind is a torn read; today only a documentation warning prevents it.

## What Changes

- **Trace daemon requires the time base.** `TraceDaemonSession::start()` attaches to the published base (or uses an already attached one) and fails startup when no usable base exists. `resolve_snapshot_utc()` resolves only through `TimeBaseClock`; a trigger whose instant cannot be resolved fails the snapshot write and is counted, and the daemon never reads `CLOCK_REALTIME`. Remove the startup offset, the fallback latch, and the stderr fallback report.
- **`logger()` and `metrics()` become fallible.** Both return `core::Result<T*>` and report `NotInitialized` before successful initialization and after `shutdown()` began, matching `meter()`/`tracer_for()`. `chrono()` keeps its documented lifetime exception. All call sites migrate.
- **`bind_diagnostics` is configuration-only.** `MetricProvider::bind_diagnostics` and `TracerProvider::bind_diagnostics` return `core::Status` and report `Conflict` while the provider worker is running or after the provider is sealed. Construction-time binding remains the supported path; the templated convenience forwards the status.
- No recording/formatting hot-path changes and no new allocations.
- **BREAKING (API):** `logger()`/`metrics()` change return type; `bind_diagnostics` changes from `void` to `core::Status`. Source-compatible migrations are applied to this repository; downstream callers must check the new `Result`/`Status`.
- The daemon's snapshot naming becomes dependent on a usable base: an unavailable base at startup is a startup failure, not a degraded write.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `tracing/trace-daemon`: snapshot UTC resolution requires the published base; the startup raw-offset fallback and stderr report are removed; unresolved trigger instants fail the snapshot instead of writing a fallback-derived name.
- `app/application`: the logger and metrics accessors are fallible (`NotInitialized` before initialization and after shutdown); the chrono accessor remains the documented exception.
- `metrics/metric-provider`: the diagnostics binding is configuration-only and reports `Conflict` on a running or sealed provider.
- `tracing/tracer-provider`: the diagnostics binding is configuration-only and reports `Conflict` on a running or sealed provider.

## Impact

- **Code**: `include/nexigen/tracing/detail/trace_daemon_session.hpp`; `include/nexigen/app/infrastructure.hpp`; `include/nexigen/metrics/metric_provider.hpp` + `src/metrics/provider.cpp`; `include/nexigen/tracing/tracer_provider.hpp`. Call-site migration: ~47 `logger()` uses and 4 `metrics()` uses across `tests/`, `examples/`, and tools; `bind_diagnostics` call sites in `infrastructure.hpp` (chrono unchanged) and provider tests.
- **Tests**: daemon startup without a base fails; trigger without a resolvable base fails and is counted; no `CLOCK_REALTIME` reads; pre-init and post-shutdown `logger()`/`metrics()` return `NotInitialized`; live `bind_diagnostics` returns `Conflict`; constructor binding still reports transitions.
- **Docs**: `docs/trace-daemon.md` (required base, no fallback), `README.md`/`docs/architecture.md` accessor examples, ADR 0001 cross-reference if needed.
- **Performance**: no hot-path change. The daemon loses one cold startup syscall pair; accessors add one `initialized_` check; `bind_diagnostics` adds one joinable/sealed check. Memory: the daemon drops two scalar fields; no layout change elsewhere.
