# Proposal

## Why

Telemetry providers have no way to report their own lifecycle and runtime failures. `MetricProvider` discards the status of every background collection (`static_cast<void>(collect_now())`), `TracerProvider` only surfaces delivery errors when someone calls the blocking `flush()`, and `TimeBaseClient::attach` fails before a logger exists, so the reason is reduced to a bare `ErrorCode` at the process entry point. The application is left to poll statuses and print to stderr.

Two time-source deviations recorded in ADR 0001 make this worse: metrics compute `MetricBatchSnapshot.utc_ns` from a raw `CLOCK_REALTIME − CLOCK_MONOTONIC_RAW` sample read during harvest, and `trace_daemon_session` attaches to the published time base but still derives UTC from a raw offset sample. Both violate the "runtime never reads `CLOCK_REALTIME`" rule and neither reports when UTC is unresolved.

This change gives every telemetry subsystem a single, uniform, cold-path diagnostics channel and closes both ADR 0001 deviations. It builds on the quiescence contract: the logger is already the last component to stop, so it can receive every shutdown diagnostic.

## What Changes

- Add a `core` diagnostics vocabulary: `DiagnosticSource`, `DiagnosticKind`, `DiagnosticEvent`, a type-erased `DiagnosticBinding` with a no-op default, a narrow `DiagnosticLogger` concept, and a null implementation. Every public type follows the project serialization mandates (`to_string`, `std::formatter`, `to_json`/`from_json` for enums).
- Add `bind_diagnostics(DiagnosticBinding)` plus a templated `bind_diagnostics(L&)` convenience to `TimeBaseClient`, `MetricProvider`, and `TracerProvider`. The classes stay non-template; only the small adapter is instantiated at the call site.
- Add a synchronous `BootstrapDiagnostics` writer (fixed buffer, `write(2)`, monotonic stamps, no rings, no worker, no time base) and a `DiagnosticBinding` parameter on `TimeBaseClient::attach`, so attach failures are reported before the logger exists.
- Wire diagnostics in `Infrastructure`: bootstrap → attach → logger start → bind chrono/metrics/tracing to the logger adapter → metrics → tracing. Initialization failures of metrics/tracing are logged once the logger is ready (before rollback).
- Provider events are cold-path only and transition-only: `Started`, `Degraded`, `Recovered`, `Quiesced`. Providers retain `harvest_status()` / `delivery_status()` for observability and tests, while runtime transitions are emitted through the binding; time-base state transitions (`Attached → Holdover → Lost`) use the same channel.
- **BREAKING (internal format):** `MetricBatchSnapshot.utc_ns` is resolved through `TimeBaseClock::utc_from_canonical` and carries a resolved/unresolved marker; the raw-offset path is removed. The trace daemon resolves snapshot UTC through the attached time base and never reads `CLOCK_REALTIME` on the trigger path.
- No recording/formatting hot path changes, no new hot-path branches, and no new allocations on any recorded path. Diagnostics add one 16-byte binding per provider/client and a cold-path indirect call per event; the bootstrap writer is used only during startup.

## Capabilities

### New Capabilities

- `core/diagnostics`: the shared cold-path diagnostic vocabulary, binding, logger concept, and null implementation used by every telemetry subsystem.

### Modified Capabilities

- `chrono/time-base`: attach accepts a diagnostic binding and reports attach failure/transition events; the client exposes `bind_diagnostics` and reports `Attached/Holdover/Lost` transitions.
- `metrics/metric-provider`: runtime harvest/flush status is retained and reportable; diagnostics binding; UTC in the batch snapshot resolves through the published time base with a resolved/unresolved marker.
- `tracing/tracer-provider`: runtime delivery status is retained and reportable; diagnostics binding.
- `tracing/trace-daemon`: snapshot UTC resolves through the attached time base; the trigger path performs no `CLOCK_REALTIME` read and reports unresolved time.
- `app/application`: bootstrap diagnostics before the time base; binds the subsystem diagnostics to the policy logger; logs metrics/tracing initialization failures before rollback.

## Impact

- **Code**: new `include/nexigen/core/diagnostic_*.hpp` and `core/concepts/diagnostic_logger_concept.hpp`; `include/nexigen/app/detail/bootstrap_diagnostics.hpp` and `diagnostic_forwarder.hpp`; `chrono/time_base_client.hpp`; `metrics/metric_provider.hpp` + `src/metrics/provider.cpp` + `metric_batch_snapshot.hpp`; `tracing/tracer_provider.hpp` + `tracing/detail/trace_daemon_session.hpp`; `app/infrastructure.hpp` wiring.
- **Tests**: transition-once and no-op-unbound diagnostics; bootstrap writes attach failures; no `CLOCK_REALTIME` read from runtime telemetry paths; UTC resolved/unresolved round-trips; app checkpoint logs transitions; no diagnostics after quiescence.
- **Docs**: `docs/architecture.md` diagnostics channel, `docs/metrics-guide.md`, `docs/trace-daemon.md`, `docs/recorder.md` (UTC marker alignment), and a new ADR for the diagnostics policy.
- **Performance**: hot paths unchanged — counter `inc()` 2 ns gate, span start/log-record paths keep their existing instruction sequences; all reporting is cold-path and transition-only. Memory: +16 bytes per provider/client, no new threads or buffers beyond the fixed bootstrap buffer; no allocation on diagnostics emission.
