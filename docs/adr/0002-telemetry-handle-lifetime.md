# ADR 0002 — Telemetry handle lifetime spans the Infrastructure object

- **Status:** Accepted
- **Date:** 2026-11-21
- **Scope:** `nexigen::app`, `nexigen::logging`, `nexigen::metrics`, `nexigen::tracing`
- **Change:** `openspec/changes/keep-telemetry-handles-valid-until-infrastructure-destruction`

## Context

An application has one main-scoped lifecycle: build the `Infrastructure`, initialize it, run the
program, shut down, and destroy the object at the end of `main`. The infrastructure hands out
borrowed telemetry handles: `metrics::Meter*` and instrument pointers, `tracing::Tracer*` and
`ScopedSpan` guards, and `Logger&`. A component creates its instruments through a `Meter` and
keeps them as raw members (`docs/architecture.md`, "Компонент получает `Meter` и создаёт
инструменты сам").

Before this decision, `Infrastructure::shutdown()` reset every component, freeing the metric
arena, the tracer pool, the logger rings, and the sinks while `main` continued. Any handle use
after that line — a counter in a destructor, a tracer in a cleanup branch, a log in an error path
— was silent use-after-free. The existing liveness checks (`MetricProvider::alive_`,
`TracerProvider::alive_`) only covered provider entry points and were documented as safe only
while the storage remained mapped, which a destructor does not guarantee.

The ownership answer is fixed and cannot be shared: one provider owns one pre-allocated block.
`shared_ptr` would either fake ownership (no-op deleter, dangling anyway) or move shutdown into
reference counts, breaking the deterministic drain/flush order.

## Decision

1. **Borrowed handles, one owner.** `MetricProvider` (and its `Meter`/instruments),
   `TracerProvider` (and its `Tracer`/`ScopedSpan` handles), and the policy logger are owned by
   the `Infrastructure`. Handles are borrowed pointers/references: the caller must never
   destroy, free, or outlive them.

2. **`shutdown()` quiesces; `~Infrastructure` destroys.** `shutdown()` stops the signal watcher,
   logs the final record, quiesces tracing, quiesces metrics, flushes and quiesces the logger,
   detaches the time base, restores the signal mask, and releases signal ownership. It does not
   destroy storage or sinks. A handle is memory-safe exactly while the `Infrastructure` object is
   alive; `~Infrastructure` releases the storage in reverse declaration order (providers before
   sinks, logger last among telemetry).

3. **Quiescence is terminal and idempotent.** Every quiescence step stops and joins its
   background worker after a final drain, flushes before sealing, and marks the provider sealed.
   A sealed provider performs no second drain or sink I/O at destruction. A repeated call
   succeeds without contacting the sink. There is no resume and no re-initialization after
   shutdown (`initialize` reports `NotInitialized`).

4. **Control plane closes; recording stays memory-safe.** After quiescence, every entry point
   that creates handles or triggers collection/flush returns `NotInitialized`
   (`get_meter`/`meter`, `get_tracer`/`tracer_for`, `collect_now`, `flush`). Recording and
   reading on already-issued handles remain memory-safe; their effect depends on the subsystem:
   | Subsystem | After quiescence |
   |---|---|
   | Metrics instrument | accepted and never collected |
   | Tracing span start | inactive guard with `NotInitialized` |
   | Logging record | accepted, dropped by the existing ring overflow policy |

5. **Order is part of the contract.** Logger starts first (after the time base) and is quiesced
   last, so tracer/metric failures are loggable. Shutdown order is tracing → metrics →
   logger.flush → logger.quiesce. Tracing before metrics lets the final metric harvest capture
   tracer counters. The accepted loss is that counter updates from the logger's final drain are
   not harvested.

6. **No hot-path guards.** No liveness flag is read by `Counter::inc()`, span starts beyond the
   existing `alive_` load, or log-record paths. Detection of use after `~Infrastructure` is the
   sanitizer's job (ASan/TSan builds), not a per-record branch.

7. **Logger is not injected into providers.** Providers return `Status` and expose diagnostics;
   the application decides what to log. `shutdown()` logs tracer/metric quiescence failures
   while the logger is still active. A dedicated diagnostics channel
   (`BootstrapDiagnostics`/`DiagnosticBinding`) is a separate, follow-up decision.

8. **`chrono` is the documented exception.** The `TimeBaseClient` handle (`chrono()`) stops being
   usable at shutdown; extending the invariant to the time-base client is out of scope.

## Consequences

**Positive**

- One invariant, easy to state and test: handles are valid for the `Infrastructure` lifetime.
- The realistic use-after-free window (explicit `shutdown()` followed by more `main` code,
  component destructors recording final values) disappears.
- Destructors never perform sink I/O after shutdown; no duplicate batch.
- No hot-path cost and no new allocation: quiescence is cold-path control work.

**Negative / accepted**

- Component storage stays resident until `~Infrastructure` (~3.3 MiB metrics, ~10 MiB tracing,
  logger shards by configuration). Peak allocation is unchanged.
- Effects of post-quiescence recording are silently discarded (metrics) or dropped (logging);
  spans fail visibly. Applications must stop producer threads before shutdown anyway.
- The metric final harvest reads `SingleThreaded` instruments, so producer threads must be joined
  before shutdown — the same precondition every harvest already has.
- Use after `~Infrastructure` remains undefined behavior; sanitizers and this contract are the
  mitigation.
- The logger's own quiescence failure can only be returned, not logged.

## Alternatives considered

- **`shared_ptr<Counter>` with a no-op deleter.** Rejected: control-block allocation violates the
  zero-allocation creation requirement, the refcount does not control storage, and the type
  advertises sharing that must not happen.
- **`shared_ptr` that keeps the provider alive.** Rejected: shutdown becomes refcount-driven and
  loses the deterministic drain/flush order.
- **Per-record liveness checks.** Rejected: a load/branch on the 2 ns `inc()` gate, and unsound
  once the arena is unmapped.
- **Raw pointers plus documentation only.** Rejected: leaves the explicit-shutdown window as a
  silent UAF and contradicts the stated invariant.
- **Templating providers on the logger.** Rejected: forces the metric provider header-only,
  cascades the logger type into `ScopedSpan`, and makes `chrono` depend on `logging`.

## References

- `openspec/specs/app/application/spec.md` — shutdown ordering, accessor contract.
- `openspec/specs/metrics/metric-provider/spec.md` — explicit quiescence.
- `openspec/specs/tracing/tracer-provider/spec.md` — quiescence and dead handles.
- `openspec/specs/logging/logger-quiescence/spec.md` — logger quiescence contract.
- `docs/adr/0001-time-source-policy.md` — the detach boundary depends on this order.
