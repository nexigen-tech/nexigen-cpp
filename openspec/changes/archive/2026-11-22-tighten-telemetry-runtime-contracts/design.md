# Design

## Context

See `proposal.md` and the four delta specs. The three gaps are independent but share one theme: remove states in which a contract is documented but not enforceable.

- Accessors: `logger()`/`metrics()` return references into engaged optionals; before initialization the optional is empty, so the call is undefined behavior rather than `NotInitialized`. `meter()`/`tracer_for()` already return `Result<T*>`.
- Binding: `bind_diagnostics` is a plain `void`; the 16-byte value is read from the worker, so only a documentation warning prevents a live rebind.
- Daemon: `start()` samples `CLOCK_REALTIME` once and stores the offset as a fallback; a trigger then uses it when the base cannot resolve. ADR 0001 permits that offset only for contexts without a base.

## Goals / Non-Goals

**Goals:**

- Every accessor except the documented `chrono()` exception has a failure channel and uses it before initialization and after shutdown.
- Diagnostics binding is a configuration-time operation with an explicit rejection on a live or sealed provider.
- The trace daemon has no wall-clock fallback: it requires a base and fails the snapshot when an instant is unresolvable.
- No hot-path changes.

**Non-Goals:**

- Resolving the `chrono()` lifetime exception (separate decision).
- Adding an app-level diagnostics poller; providers still emit transitions themselves.
- Changing the metric batch/daemon UTC marker contract.

## Decisions

### D1 — Accessors return `core::Result<T*>`

`logger()` and `metrics()` become:

```cpp
[[nodiscard]] core::Result<Logger*> logger() noexcept;
[[nodiscard]] core::Result<MetricProvider*> metrics() noexcept;
```

Both return `std::unexpected(NotInitialized)` unless `initialized_` is true, matching `meter()`/`tracer_for()`. Returning `Result<T*>` rather than `Result<T&>` keeps `std::expected` trivially assignable and mirrors the existing factories. `chrono()` stays a reference: it is the documented exception until a separate decision.

*Call-site pattern:* `auto log = infra.logger(); if (!log) { ... } (*log)->info(...)`. Examples that already returned on initialization failure use `(*infra.logger())->...` after the success check; tests assert the `Result`.

*Alternative:* `std::optional<T*>` (no error code, inconsistent) or `Result<T&>` (not supported by `std::expected` cleanly).

### D2 — Binding only in configuration mode

`bind_diagnostics(DiagnosticBinding) noexcept -> core::Status` on `MetricProvider`/`TracerProvider`:

- `if (sealed_) return Conflict;`
- `if (worker_.joinable()) return Conflict;` (metrics: harvester; tracing: consumer)
- otherwise store the binding, reset `last_reported_error_`, and emit the current state as before.

Construction-time binding stays the primary path (`MetricProvider(config, sink, binding)` and `TracerProvider(config, sink, binding)`); the setter is for tests and configuration code that constructs a worker-less provider. The templated convenience returns the same `Status`.

The check is cold-path only; no hot path reads the binding. We deliberately do not make the binding atomic: a lock-free 16-byte atomic is not guaranteed, and a lock in the consumer is worse than rejecting the unsupported operation.

### D3 — Daemon requires the base

`TraceDaemonSession::start()`:

1. If `TimeBaseClock::attached()` is already true (tests, suites), use it.
2. Otherwise call `TimeBaseClock::attach("/nexigen-timebase")`; a failure is returned from `start()` before ports are opened.
3. No `sample_utc_offset` call, no `startup_utc_offset_ns_`, no `fallback_reported_`.

`resolve_snapshot_utc(mono_ns)` becomes `std::optional<std::int64_t>` and returns only `TimeBaseClock::utc_from_canonical(mono_ns)`. `write_snapshot` returns `NotInitialized` when it is `nullopt`; the session counts the failure in `dump_failures_` and continues draining. The snapshot is not written with a fallback-derived name.

*Consequences:* the daemon cannot start without a published base. Test binaries install a test base before starting a session; the existing e2e helpers already run inside a process that can publish one.

### D4 — Memory and ordering

- Accessors read `initialized_` (plain `bool`, control thread) once; no atomics.
- `bind_diagnostics` reads `sealed_` (acquire) and `worker_.joinable()` (joinable is internally synchronized); both cold.
- The daemon removes two scalar fields; no layout change elsewhere.
- No new per-thread shards or cache-line alignment changes.

### D5 — Migration and docs

- Update every `logger()`/`metrics()` call site (tests, examples, tools) to the `Result` form.
- Update `bind_diagnostics` call sites to check the returned `Status`.
- `docs/trace-daemon.md`: required base, no fallback, unresolved trigger failure.
- `README.md` and `docs/architecture.md` examples: fallible accessors.
- No ADR change: ADR 0001 decision 6 already states the raw offset is only for contexts without a base; the daemon now complies.

## Risks / Trade-offs

- [Breaking API] → Source migration is mechanical; downstream users must check `Result`/`Status`. The change is called out in the proposal.
- [Daemon cannot run without a base] → Intended; a missing base is a startup failure instead of silently naming snapshots from a stale offset. Tests publish a base.
- [Unresolved trigger loses a snapshot] → The incident snapshot is not written with fabricated UTC; the failure is counted and visible. A resolvable base is a deployment requirement, matching the application's time-base dependency.
- [Accessors rejected after shutdown while handles remain valid] → Deliberate: creation/access is closed, borrowed handles still point at live storage until `~Infrastructure`.
- [Configuration-mode check races a concurrent `initialize()`] → Both are control-plane operations on one thread; the constructor path is the documented multi-thread-safe one.

## Migration Plan

1. Change `logger()`/`metrics()` signatures and migrate call sites; add pre-init/post-shutdown tests.
2. Change `bind_diagnostics` to `Status` with `Conflict`; migrate call sites; add live/sealed rejection tests.
3. Daemon: require base, remove fallback, optional `resolve_snapshot_utc`, dump-failure counting; update daemon tests and docs.
4. Run the full matrix (Debug/Release/ASan/TSan) and examples.

Rollback is a revert: no persisted format changes, and the previous behavior remains source-compatible only in the reverse direction (callers already migrated would need to drop the checks).
