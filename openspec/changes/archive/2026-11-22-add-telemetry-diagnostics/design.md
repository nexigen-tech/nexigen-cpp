# Design

## Context

See `proposal.md` for motivation and `specs/` for the behavior contract. Two current facts shape the approach:

- Providers already run a control plane (harvester/consumer) and expose `Status`; they just discard or defer errors. `MetricProvider::harvest_loop` calls `collect_now()` with `static_cast<void>` (`src/metrics/provider.cpp`), and `TracerProvider::deliver_batch` ignores the sink status (`tracer_provider.hpp`), surfacing only `flush()`.
- `TimeBaseClient::attach` runs before the logger exists (the file sink needs the published base), so its failure can only be reported through something that works without a time base.

The quiescence contract from the previous change keeps the logger alive through tracing/metrics quiescence, so a logger-bound diagnostics channel covers every shutdown event. See ADR 0002.

## Goals / Non-Goals

**Goals:**

- One cold-path diagnostics channel shared by chrono, metrics, and tracing, with no provider-to-provider coupling and no dependency on the logging module.
- Provider errors become visible without an application polling `flush()`.
- Pre-logger time-base failures are visible through a synchronous bootstrap writer.
- Close the two ADR 0001 deviations (metrics batch UTC, trace daemon trigger UTC) without runtime `CLOCK_REALTIME` reads.
- Hot paths, shard layouts, and recording instruction sequences unchanged.

**Non-Goals:**

- No logger injection into providers and no provider-internal formatting: the app adapter formats.
- No per-record or per-batch diagnostics; no watchdog thread; no diagnostics after quiescence.
- No change to `MetricFrame`'s wire layout or to span/record layouts.
- No changes to the quiescence contract itself.

## Decisions

### D1 — Diagnostics vocabulary lives in `core`

`DiagnosticSource`, `DiagnosticKind`, `DiagnosticEvent`, `DiagnosticBinding`, `DiagnosticLogger`, and `NullDiagnosticLogger` live in `nexigen::core` (file-per-entity), because `chrono` (L1), `metrics`, and `tracing` all depend on `core` but must not depend on `logging`. Enums implement `to_string`/`from_string`/`std::formatter`/`to_json`/`from_json`; the event implements `to_string`/JSON; the binding follows the `core::SinkBinding` precedent (function pointers, no virtual dispatch, no RTTI).

*Alternative:* naming the full `logging::concepts::Logger` in chrono would pull `FormatWithLocation` into L1 and create a cycle.

### D2 — Type-erased binding + templated convenience

Providers stay non-template:

```cpp
void bind_diagnostics(DiagnosticBinding binding) noexcept;
template <concepts::DiagnosticLogger L> void bind_diagnostics(L& logger) noexcept;
```

Only the adapter lambda is instantiated at the call site. This follows `core::SinkBinding` and avoids making `MetricProvider` header-only, cascading `TracerProvider` into `Tracer`/`ScopedSpan`, or templating the compiled `TimeBaseClient`.

*Alternatives rejected:* a logger template parameter (header-only metrics, hot-path type cascade, chrono/logging cycle) and virtual dispatch (project forbids RTTI/virtual dispatch in telemetry).

### D3 — Bind before the worker starts; live rebind only on a control thread

A `DiagnosticBinding` is 16 bytes and is read from the worker thread when an event fires. To avoid torn reads without an atomic:

- `MetricProvider` gains a constructor overload `(config, sink, DiagnosticBinding)`; the harvester is started after the binding is stored.
- `TracerProvider` gains a binding parameter on its constructor; the app binds before `initialize()` starts the consumer.
- `TimeBaseClient::attach` takes the binding (default `{}`); `bind_diagnostics` on the client is control-thread-only (no worker).
- `bind_diagnostics` on a running provider is documented as requiring the worker to be stopped; the app never uses it that way. Tests rebind only on a quiesced provider.

### D4 — Emission discipline

- Closed event set: `Started`, `Degraded`, `Recovered`, `Quiesced`.
- Cold paths only: startup, quiescence, and the first error transition. Never from `inc()`/`span start`/`record`, never from a destructor.
- Transition-only: each emitter keeps `last_reported_error_` and emits only when the error code changes or recovery occurs.
- `Started` is emitted when a binding is installed on a healthy component (so late binding still reports once); `Quiesced` is emitted once at the start of quiescence, before sealing; nothing after.
- Emitters never call `flush()` or any blocking operation through the binding.

### D5 — Status storage and memory orders

| Component | Field | Writer | Reader | Ordering |
|---|---|---|---|---|
| `MetricProvider` | `std::atomic<std::uint8_t> last_harvest_error_{0}` (ErrorCode+1) | harvester / `collect_now` under `harvest_mutex_` | `harvest_status()` (cold) | relaxed store, acquire load; the mutex orders writers |
| `MetricProvider` | `core::ErrorCode last_reported_{Ok}` (plain) | under `harvest_mutex_` | same | mutex |
| `TracerProvider` | `std::atomic<std::uint8_t> last_delivery_error_{0}` (ErrorCode+1) | consumer thread only (`deliver_batch`, `publish_sync`) | `delivery_status()` (cold) | relaxed store, acquire load |
| `TracerProvider` | `core::ErrorCode last_reported_{Ok}` (plain) | consumer thread | same | single-writer |
| `TimeBaseClient` | `ClientState last_reported_state_` (plain) | application control thread (`on_tick`, `attach`, `bind_diagnostics`) | same | single-thread ownership, documented |

No hot path reads any of these. The existing `sealed_`/`alive_` release/acquire pairs are untouched.

### D6 — Time-base attach and transitions

`attach(segment, config, DiagnosticBinding = {})` stores the binding before validation. A failure emits one `Degraded` (source `Chrono`, the failure code) through the supplied binding and returns the error. On success the client stores the binding; `bind_diagnostics` replaces it and emits `Started` when the state is `Attached`. `on_tick` compares the computed `ClientState` with `last_reported_state_` and emits `Degraded` on entering `Holdover`/`Lost`, `Recovered` on returning to `Attached`. Conversion functions never emit.

### D7 — Metrics UTC resolution

`MetricBatchSnapshot` gains `bool utc_resolved{false}` (layout: one byte plus padding; the struct remains trivially copyable). At harvest, `utc_ns` is `TimeBaseClock::utc_from_canonical(stamp)`; when it returns `nullopt`, `utc_resolved` is false and `utc_ns` stays 0. JSON writes `utc_resolved` and omits `utc_ns` when unresolved. `sample_utc_offset` is removed from the provider. `MetricFrame` keeps only the monotonic `timestamp_ns`; the daemon resolves UTC, so the wire layout is unchanged.

### D8 — Trace-daemon UTC resolution

At session start the daemon samples `sample_utc_offset` once into `startup_utc_offset_ns_` (fallback). `resolve_utc(mono)` first calls `TimeBaseClock::utc_from_canonical(mono)`; on `nullopt` it uses the startup offset and sets `fallback_reported_`, printing one stderr line per session. `write_snapshot` uses `resolve_utc` for the file name and header. No `CLOCK_REALTIME` read occurs on the trigger path.

### D9 — App wiring

`build_time_and_logging` constructs the bootstrap writer before attaching, passes its binding to `TimeBaseClient::attach`, then starts the logger, then constructs metrics and tracing with the logger adapter binding. A `bool logger_ready_` distinguishes "optional engaged" from "initialized"; `fail_logged(status, stage)` logs one error before `rollback()` for metric/tracing failures (the logger is guaranteed alive). The logger adapter is a small `app::detail::DiagnosticForwarder<Logger>` member declared after the logger and before the providers, so it outlives them.

### D10 — Quiescence interaction

`Quiesced` is emitted at the start of `quiesce()`, before `sealed_`/`alive_` change, so the event is delivered while the provider can still report. After sealing, `bind_diagnostics` and event emission are no-ops. Providers are destroyed after quiescence; destructors never emit. This preserves the previous change's "no sink I/O at destruction" guarantee.

### D11 — Memory layout

- `DiagnosticEvent`: three `uint8_t`-underlying enums (source, kind, error) → 3 bytes; `static_assert(sizeof(DiagnosticEvent) == 3)`.
- `DiagnosticBinding`: `void*` + function pointer → 16 bytes on x86-64; no `alignas`.
- Per provider/client: +16 bytes binding, +1 byte status atomic, +1 byte last-reported (padding), +1 byte `utc_resolved` in the batch. None of these sits in an instrument arena or a per-thread shard; no `alignas(64)` change and no false-sharing risk. Shard sizing (`ShardCapacity`, `MaxThreads`, `max_*`) is untouched.

### D12 — Bootstrap writer

`app::detail::BootstrapDiagnostics` implements `DiagnosticLogger`: a fixed 256-byte stack buffer, `std::format_to_n` (no allocation), a `CLOCK_MONOTONIC_RAW` prefix from `Timestamp::now()`, and `::write(STDERR_FILENO, ...)` with EINTR retry. It has no rings, no worker, no time base, and ignores write failures. Its lifetime is the initialization window; it is not used after the logger is bound.

### D13 — ADR

Record this channel and the "no runtime REALTIME" rule in `docs/adr/0003-telemetry-diagnostics.md`, referencing ADR 0001 and 0002.

## Risks / Trade-offs

- [Binding read without an atomic] → Bind before worker start; live rebind is a documented control-thread operation; tests rebind only when quiesced.
- [Flapping sink produces a log line per Ok↔error transition] → Accepted and documented; transition-only is still bounded by the sink's actual state changes. No hysteresis in this change.
- [`utc_resolved=false` changes JSON consumers] → Omit `utc_ns` and add the marker; align `docs/recorder.md` and tests; the recorder already distinguishes unresolved time.
- [Diagnostics lost when the log ring is full] → Same behavior as any log record; bootstrap covers pre-logger failures, and the drop summary reports the loss.
- [Daemon fallback offset ages during a long session] → Reported once; the published base is preferred on every trigger, so the fallback is used only when resolution is unavailable.
- [Attach signature changes] → Default argument keeps source compatibility; the static library is rebuilt. ABI change is limited to chrono consumers.
- [Reentrancy] → Metrics emits after leaving `harvest_mutex_`; tracing emits on its consumer; the logger producer never calls providers, so no feedback loop.

## Migration Plan

1. Core vocabulary + binding + concept + null implementation (with serialization tests).
2. `TimeBaseClient::attach` binding + `bind_diagnostics` + transitions.
3. `MetricProvider`: status atomic, transitions, binding constructor, UTC marker.
4. `TracerProvider`: delivery status, transitions, binding constructor.
5. Trace daemon: `resolve_utc`, startup fallback, stderr report.
6. `Infrastructure`: bootstrap, bindings, `logger_ready_`, `fail_logged`.
7. Tests and docs; ADR 0003.

Rollback is a straight revert: no persisted data, no configuration change, and the previous status/flush paths remain source-compatible. `MetricBatchSnapshot`'s layout change requires rebuilding consumers; the wire `MetricFrame` is unchanged.

## Open Questions

None. The previously open points are decided here: construction-time binding (D3), transition-only without hysteresis (D4), `utc_resolved` marker (D7), and one startup fallback for the daemon (D8).
