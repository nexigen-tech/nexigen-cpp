# ADR 0003 — Telemetry diagnostics channel

- **Status:** Accepted
- **Date:** 2026-11-21
- **Scope:** `nexigen::core`, `nexigen::chrono`, `nexigen::metrics`, `nexigen::tracing`, `nexigen::app`
- **Change:** `openspec/changes/add-telemetry-diagnostics`

## Context

Providers had no uniform way to report lifecycle and runtime failures. Metrics discarded every background collection status, tracing surfaced delivery errors only through the blocking `flush()`, and time-base attach failed before a logger existed. The two ADR 0001 deviations (metrics batch UTC and the trace-daemon trigger path reading `CLOCK_REALTIME`) compounded the problem because neither reported an unresolved time.

ADR 0002 established that the logger is the last telemetry component to stop, so it can receive every shutdown diagnostic. This ADR defines the channel that carries those diagnostics.

## Decision

1. **Vocabulary in `core`.** `DiagnosticSource`, `DiagnosticKind`, `DiagnosticEvent`, `DiagnosticBinding`, the `DiagnosticLogger` concept, and `NullDiagnosticLogger` live in `nexigen::core`. Chrono (L1) and the providers use the concept without depending on `nexigen::logging`; the format wrapper never enters L1.

2. **Type-erased binding, typed convenience.** `DiagnosticBinding` is a borrowed pointer plus a `noexcept` callback (null = no-op). Providers expose `bind_diagnostics(DiagnosticBinding)` and a templated overload for any `DiagnosticLogger`; the class types stay non-template. This follows `core::SinkBinding` and avoids header-only metrics, a logger type parameter on `ScopedSpan`, and a chrono/logging cycle.

3. **Bind before the worker starts.** A provider receives the binding at construction (metrics) or before `initialize()` (tracing); the time-base client stores it during `attach` and rebinds on the control thread. Live rebinding on a running worker is not supported, which keeps the 16-byte value free of atomics.

4. **Transition-only, cold-path emission.** Emitters report `Started`, `Degraded`, `Recovered`, and `Quiesced` at most once per state change. No event is emitted from a recording path, a per-record/per-batch loop, or a destructor. `Quiesced` is emitted before sealing; nothing is emitted afterwards.

5. **No logger injection.** Providers do not know the logging module and never call `flush()`. The application adapts its policy logger into a `DiagnosticForwarder`, which defers formatting to the logger's consumer.

6. **Pre-logger bootstrap.** `BootstrapDiagnostics` writes synchronous, allocation-free stderr lines with a monotonic prefix. `TimeBaseClient::attach` accepts the binding and reports one `Degraded` before returning an attach failure; after the logger exists, the client is rebound to it and reports `Started`/`Holdover`/`Lost` transitions.

7. **UTC is resolved once, at the presentation boundary.** Runtime telemetry never reads `CLOCK_REALTIME`. The metric batch carries `utc_resolved` and resolves through `TimeBaseClock`; the trace daemon resolves snapshot UTC through the attached base and reuses the startup-sampled offset only as a once-per-session fallback, reported on stderr. This closes the two ADR 0001 deviations.

8. **Observability remains pullable.** Providers retain `harvest_status()` / `delivery_status()` for tests and external monitors, read without I/O. They do not replace the event stream.

## Consequences

**Positive**

- One uniform, testable diagnostics path for all telemetry subsystems, with no module cycle.
- Provider errors become visible without an application polling `flush()`.
- Attach failures are visible before the logger exists.
- No hot-path cost: +16 bytes per provider/client, one cold indirect call per transition, no allocation.
- Runtime telemetry paths perform no `CLOCK_REALTIME` read; unresolved UTC is explicit.

**Negative / accepted**

- A flapping sink produces one log line per Ok↔error transition. No hysteresis is applied in this version.
- Diagnostics are lost if the log ring is full, like any log record; the bootstrap channel covers pre-logger failures.
- `MetricBatchSnapshot` gained a field (layout change); consumers are rebuilt. The `MetricFrame` wire layout is unchanged.
- The daemon's fallback offset ages during a very long session; the published base is preferred on every trigger.
- Live rebinding on a running worker is forbidden; integrations bind before start.

## Alternatives considered

- **Template providers on the logger type.** Rejected: header-only metric provider, `ScopedSpan` type cascade, and a chrono/logging dependency.
- **A virtual diagnostics interface.** Rejected: the project forbids virtual dispatch/RTTI in telemetry.
- **Application-side polling only.** Rejected: it delays error visibility and duplicates the transition logic the emitters already own.
- **Sampling `CLOCK_REALTIME` on every trigger in the daemon.** Rejected by ADR 0001; the startup fallback is the bounded exception.

## References

- `docs/adr/0001-time-source-policy.md` — runtime `CLOCK_REALTIME` prohibition and UTC resolution.
- `docs/adr/0002-telemetry-handle-lifetime.md` — quiescence order that keeps the logger alive for shutdown diagnostics.
- `openspec/specs/core/diagnostics/spec.md` — the channel contract.
- `openspec/specs/{chrono/time-base,metrics/metric-provider,tracing/tracer-provider,tracing/trace-daemon,app/application}/spec.md` — per-subsystem requirements.
