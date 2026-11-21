# Design

## Context

See `proposal.md` — Why, and the delta specs under `specs/` for the behavior contract. The current implementation ties component lifetime to `Infrastructure::shutdown()`: `infrastructure.hpp:591-594` resets `tracer_`, `trace_sink_`, `metrics_`, `metrics_sink_`, and `logger_`, so any handle issued before that line dangles while `main` continues. Handles are borrowed raw pointers by contract (`docs/architecture.md:44-45`); the provider is the sole owner, and recording is the only hot path.

Two existing behaviors constrain the approach:

- `~MetricProvider` unconditionally performs a final `collect_now()` + sink flush (`src/metrics/provider.cpp:137-148`); `~TracerProvider` has an `else if` branch that re-drains inline when the worker was never joined (`tracer_provider.hpp:685-708`). Both would produce a second sink I/O at `~Infrastructure` unless sealed.
- `AsyncWorker::flush()` returns `NotInitialized` when the worker is no longer joinable (`async_worker.hpp:325-328`), and the worker's shutdown path drains and calls `sink.stop()` (`async_worker.hpp:216-223`).

## Goals / Non-Goals

**Goals:**

- One invariant for all telemetry: handles issued by `Infrastructure` are memory-safe for the whole `Infrastructure` lifetime, and destruction is the only release point.
- Quiescence is a first-class, idempotent, allocation-free control operation on every provider and logger.
- No new hot-path instruction, branch, or allocation; no change to instrument, span, or log-record layout.
- Destruction performs no sink I/O after quiescence.

**Non-Goals:**

- Sound detection of use after `~Infrastructure` (process-lifetime handles, global state). ASan/TSan coverage remains the tool.
- Uniform post-quiescence semantics between the three subsystems; see D4.
- Restart/resume after quiescence, or re-initialization after shutdown (already rejected by `shutdown_started_`).
- `chrono`/`TimeBaseClient` lifetime; explicitly out of scope.

## Decisions

### D1 — `shutdown()` quiesces; the destructor destroys

`shutdown()` stops activity and drains; `~Infrastructure` frees storage. The members `metrics_`, `metrics_sink_`, `trace_sink_`, `tracer_`, `logger_` stay engaged after `shutdown()` and are destroyed in the existing reverse-declaration order (`tracer_ → trace_sink_ → metrics_ → metrics_sink_ → logger_`), so providers die before their sinks and the logger dies last. No manual destructor code is needed; the declaration order in `infrastructure.hpp:842-847` is already the lifetime contract.

*Alternatives:* resetting only at the end of `shutdown()` (today: the UAF window), or `shared_ptr` co-ownership (allocation per instrument, refcount-driven shutdown, contradicts the arena model).

### D2 — `sealed_` is a separate flag from `alive_`

`alive_` currently answers "is the provider object alive" for the debug liveness contract. `sealed_` answers "was quiescence performed" and drives two things only: the control-plane gates and the destructor's skip of drain/flush. Conflating them would force `collect_now()` inside the final drain to see itself as dead.

- `MetricProvider`: `std::atomic<bool> sealed_{false};` beside `alive_`.
- `TracerProvider`: `std::atomic<bool> sealed_{false};` beside `alive_`.
- Both are cold-path fields written once; `sealed_` uses `store(release)` in `quiesce()` and `load(acquire)` in gates. The existing `alive_` release/acquire pair in `start_span` is unchanged. No hot path reads `sealed_`.
- Logger: no flag. Stopping the consumer is the seal; `AsyncWorker::flush()` already returns `NotInitialized` once the worker is not joinable, and the ring's overflow policy already bounds post-quiesce writes.

### D3 — Memory layout, ordering, shard sizing

- No layout change to instruments, tracer records, rings, or shards. `MetricProvider::sealed_` and `TracerProvider::sealed_` are appended next to the existing cold control fields (`alive_`, `startup_error_`); they share a cache line with other control-plane state and with no producer cell, so no `alignas(64)` is added and no false-sharing risk is introduced.
- `TracerProvider` stays `alignas(64)`, `worker_` stays the last member (joins before the pool dies), tracer pool stride unchanged.
- Per-thread shard sizing is untouched: `MaxThreads`/`ShardCapacity` for tracing, `MaxThreads`/`ShardCapacity`/`MessageCapacity` for logging, `max_*` capacities for metrics.
- Orders: `sealed_.store(std::memory_order_release)` after the final drain; `sealed_.load(std::memory_order_acquire)` in `get_meter`/`collect_now`/`flush`, `get_tracer`/`flush`, and the destructor. The worker join provides the happens-before for all drained writes; `alive_.store(false, release)` in tracing quiescence is observed by the existing `acquire` load in `start_span`.

### D4 — Post-quiescence semantics stay per-subsystem

| Subsystem | After quiescence | Why not uniform |
|---|---|---|
| Metrics instrument | accepted, never collected | A seal check in `inc()` would add a load/branch to the 2 ns gate |
| Tracing span start | `NotInitialized` (inactive guard) | The liveness load already exists for the dead-handle contract; reusing it is free |
| Logging record | accepted, dropped when the ring fills | The consumer is gone; adding an acceptance flag would touch the format path |

All three are memory-safe and produce no sink output; only observability differs. The differences are documented in the ADR and `docs/metrics-guide.md` rather than erased with hot-path checks.

### D5 — Logger quiescence is a narrow concept

`core::concepts::Logger` stays untouched. A new `logging::concepts::QuiescableLogger` (in `include/nexigen/logging/concepts/quiescable_logger_concept.hpp`) requires `{ t.quiesce() } noexcept -> std::same_as<core::Status>`; `Infrastructure` asserts it next to the existing `static_assert(Logger<Logger>)` (`infrastructure.hpp:321`). Implementations: `NullLogger` returns success, `ConsoleLogger`/`FileLogger`/`Iceoryx2Logger` flush then `worker_.stop()`, `CompositeLogger` forwards and returns the first failure. `AsyncWorker` gains a public `stop() noexcept` that calls the existing `worker_.stop()`.

*Alternative:* extending the core `Logger` concept would force every test double and every non-app logger to implement quiescence — a module-wide breaking change for no behavioral gain.

### D6 — Quiescence order: tracing → metrics → logger

`tracer.quiesce()` drains spans and updates tracer counters; `metrics.quiesce()` then captures those counters in the final harvest; the logger remains active through both so failures and component diagnostics can be logged; `logger.flush()` → `logger.quiesce()` closes the pipeline last. Errors returned by tracer/metric quiescence are logged before the final logger flush. `shutdown()` still returns the first failure; the logger's own quiescence failure can only be returned.

*Accepted trade-off:* counter updates made by the logger's final drain are not harvested. `Infrastructure` never binds the logger to a meter, so this only affects applications that bind it themselves.

### D7 — Destructors respect the seal

`~MetricProvider`: if `sealed_`, skip `worker_.stop()`, `collect_now()`, and the sink flush; then destroy instruments, meters, and the block as today. `~TracerProvider`: if `sealed_`, skip both the join branch and the inline-drain `else if`; then publish `alive_ = false`. This removes the duplicate-batch I/O at `~Infrastructure` and is pinned by tests.

### D8 — Accessor gating moves from "optional engaged" to "initialized"

`meter()` and `tracer_for()` currently gate on `metrics_`/`tracer_` being engaged (`infrastructure.hpp:646`, `660`). Because members stay engaged after shutdown, the gates move to `initialized_`, preserving the existing `NotInitialized` result after shutdown. `metrics()`, `logger()`, and `chrono()` remain reference accessors; the first two return live quiesced objects, `chrono()` keeps its current (out-of-scope) behavior.

### D9 — `chrono` stays out

`TimeBaseClock::detach()` and `time_base_.reset()` remain in `shutdown()`; the `chrono()` handle remains unusable afterwards. Extending the invariant to the time-base client needs a separate decision about the process-global mapping and is recorded as a follow-up, not silently folded in.

### D10 — No uniform "provider fell" detection

There is no partial state in which the provider object exists but is dead: construction either succeeds (possibly with a `startup_status` failure that rolls initialization back) or the object is destroyed. The only real failure class is use after `~Infrastructure`, which is outside the supported lifecycle. Per-record liveness checks are rejected: they cost hot-path cycles and are unsound once the storage is unmapped.

## Risks / Trade-offs

- [Memory residency: registries stay mapped until `~Infrastructure`] → Defaults add ~3.3 MiB metrics + ~10 MiB tracing + logger shards to the end of `main`; peak allocation is unchanged and release is deterministic at scope exit.
- [Silent data loss after quiescence (metrics accepted, logs dropped, spans rejected)] → Documented in the ADR, `docs/metrics-guide.md`, and `README.md`; producers must stop before `shutdown()`.
- [Final harvest during metrics quiescence can race a `SingleThreaded` recorder] → Precondition: producer threads are joined before shutdown; the same rule already applies to every harvest (`docs/metrics-guide.md` §7).
- [Duplicate sink I/O at destruction] → `sealed_` gates both destructors; tests assert the sink batch count does not change during `~Infrastructure`.
- [Use after `~Infrastructure` remains UB] → Narrowed to a much rarer path; ASan builds (`build-asan`, `build-asan-com`) catch it during development; ADR states the contract.
- [`logger()`/`metrics()` after a failed initialization] → Pre-existing gap (empty `optional` dereference); rollback still resets members, and the change does not widen it.
- [Ordering regressions] → The order is pinned by the `app/application` scenarios "Logger is quiesced last" and "Quiescence failures reach the log"; a test asserts the sink shutdown sequence.

## Migration Plan

No persisted state or configuration changes; no flags. Implementation order:

1. `AsyncWorker::stop()` + `QuiescableLogger` concept + implementations + conformance assertions.
2. `MetricProvider::quiesce()` + `sealed_` gates + destructor skip.
3. `TracerProvider::quiesce()` + `sealed_` gates + destructor branch handling.
4. `Infrastructure::shutdown()` ordering, accessor gating, quiescence-error logging.
5. Tests across the four suites, then docs (`architecture.md`, `metrics-guide.md`, `README.md`) and the ADR.

Rollback is a straight revert: no on-disk format, no public signature removal, and the pre-change behavior is fully restored by deleting the quiescence calls and restoring the `reset()` lines. Build and test with `scripts/build.sh build-asan-com build-release-com` (per AGENTS.md, `-j 64`).
