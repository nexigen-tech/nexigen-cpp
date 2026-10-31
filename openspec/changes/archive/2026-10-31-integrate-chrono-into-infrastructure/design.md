# Design

## Context

`Infrastructure` (see proposal.md — Why) composes logging, metrics, and tracing through a stateless `Policy`: it owns a `Logger`, two sinks, and two providers, and drives their emplacement, startup order, and shutdown flush order. The `nexigen::chrono` clock service exists as a standalone subsystem whose `ChronoProvider` currently auto-starts its worker in the constructor.

This change wires chrono in as the time foundation, constructed first, but starts its worker only after the logger exists so the worker can log. That requires a two-phase `ChronoProvider` lifecycle (construct → start) and an optional logger binding.

## Goals / Non-Goals

**Goals:**
- Give `ChronoProvider` a two-phase lifecycle: construction provides `snapshot_now()`; `start()` (optionally with a logger) starts the worker.
- Compose `ChronoProvider` through `Infrastructure`: constructed first, worker started after the logger is bound, shutdown flush, `chrono()` accessor.
- Aggregate `chrono::Chrono_settings` into `App_settings`.

**Non-Goals:**
- No change to the clock service's snapshot/offset semantics.
- No app-level wrapper for chrono settings: chrono already owns its loader and settings types.
- No new chrono→logging dependency: the worker's log hook stays module-local.

## Decisions

### D1 — Reuse `chrono::Chrono_settings` directly

`App_settings` gains `chrono::Chrono_settings chrono{}` and `App_settings_paths` gains `chrono::Chrono_settings_paths chrono{}`. Unlike metrics/tracing (whose loaders live in the app module), chrono already owns `load_chrono_settings`, so the app module delegates rather than duplicating.

### D2 — Policy contract gains a required `Chrono_sink` + `emplace_chrono_sink`

`InfrastructurePolicy` gains `typename Policy::Chrono_sink` and `Policy::emplace_chrono_sink(std::optional<Chrono_sink>&, const chrono::Chrono_settings&) noexcept -> core::Status`, plus a `static_assert` on `chrono::concepts::ChronoSink<Chrono_sink>`. This is a breaking change to the policy contract; both in-tree policies are updated in the same change.

### D3 — Two-phase construction and shutdown order

Construction order in `initialize()`: **construct chrono provider (no worker) → emplace logger → bind logger to chrono and start its worker → metrics sink+provider → tracing sink+provider → signal watcher**.

The chrono provider is constructed first (it is the time foundation and provides `snapshot_now()` immediately), but its worker starts only after the logger exists, so the worker can log. `chrono_.emplace(settings.chrono.options, *chrono_sink_)` then `chrono_->start(logger_bridge)`.

Shutdown order: `logger flush` (drain + log the stop reason + flush) → `tracer flush` → `metrics collect+flush` → `chrono snapshot+flush`, then reset. `snapshot_now()` is safe to call while the worker runs (it takes the provider's snapshot mutex). Reset releases the chrono provider before its sink and before the logger (see D4), so the worker can still log while it stops.

### D4 — Member lifetime order

The chrono worker references its sink, the bound logger, and the logger bridge, so all three must outlive the provider. The logger bridge is a persistent member (the worker holds its address) and is declared after the logger it points at, so destruction releases the bridge before the logger but after the chrono provider:

```
std::optional<Logger> logger_{};
detail::Chrono_logger_bridge<Logger> chrono_logger_bridge_{};
std::optional<Metrics_sink> metrics_sink_{};
std::optional<metrics::MetricProvider> metrics_{};
std::optional<Trace_sink> trace_sink_{};
std::optional<Tracer_provider> tracer_{};
std::optional<Chrono_sink> chrono_sink_{};
std::optional<chrono::ChronoProvider> chrono_{};
detail::Signal_watcher signals_{};
```

Destruction releases chrono (worker) first, then its sink and the bridge, and the logger last. `rollback()` resets in the same order as `shutdown()`: `chrono_` then `chrono_sink_` before `logger_`.

### D5 — Module-local worker log hook

Dependency arrows stay one-way (`logging` already depends on `chrono`), so chrono cannot depend on logging. `ChronoProvider` therefore exposes a module-local log hook: a small `Clock_log_level` enum (`info`, `warning`, `error`) and a type-erased `detail::Log_binding { void* self; void (*log)(void*, Clock_log_level, std::string_view) noexcept; }`. `start()` overloads:

- `start() noexcept -> core::Status` — start the worker with no logger.
- `template <class Logger> start(Logger&) noexcept -> core::Status` — bind a logger-like object (anything with `log(Clock_log_level, std::string_view)`) and start the worker.

The app module bridges a `nexigen::logging` logger to this hook by mapping `Clock_log_level` → `logging::LogLevel` (`info`→`info`, `warning`→`warning`, `error`→`error`) and calling `logger.log(level, "{}", message)`.

### D6 — No new concurrent or atomic state

`Infrastructure` adds only `std::optional` members. `ChronoProvider` adds a cold-path `Log_binding` (read by the worker, written once before `start()`). Its existing `alignas(64)` atomics (`worker_mask_`, `startup_error_`) and `snapshot_mutex_` are unchanged; the deferred start simply moves the worker creation from the constructor into `start()`.

## Risks / Trade-offs

- [Breaking policy and `ChronoProvider` contract] → Both in-tree policies and the `ex_chrono_snapshot` example + chrono tests are updated in the same change; external callers must add `start()`.
- [Default config starts no chrono worker] → `App_settings{}` carries `collect_interval == 0`, so `start()` starts no thread unless configured; the snapshot path remains opt-in via `snapshot_now()`.
- [Worker log timing] → The worker logs only cold-path diagnostics (startup, pin result, snapshot failures, shutdown); the logger is flushed after the chrono provider resets so those records are durable.

## Migration Plan

Single-step: extend `ChronoProvider`, the concept, and the two in-tree policies + example + tests in one change; no staged rollout is needed.
