# Design

## Context

`nexigen::chrono` today provides value types and functions (`Timestamp`, `Stamp_source`, `Civil_time`, `sample_utc_offset`, `utc_from_mono_ns`) but no service. The three sibling subsystems (logging, metrics, tracing) share a provider/sink/settings skeleton: a pre-allocated provider owning a CPU-pinned `std::jthread` worker, a non-virtual sink concept, and a best-effort JSON settings loader. This change adds a minimal clock service to `nexigen::chrono` following that skeleton. See proposal.md — Why.

The single architectural constant that shapes everything: **the hot path stays monotonic-raw**. `Timestamp` is an 8-byte, trivially copyable value with `static_assert(sizeof == 8)`; every producer owns its own `Stamp_source` (calibrated TSC) so no shared clock is ever read on a hot path. The service is strictly control-plane: it never participates in per-record stamping.

## Goals / Non-Goals

**Goals:**
- A `ChronoProvider` that periodically records the UTC offset (`realtime − monotonic_raw`) to a file, producing a history that post-hoc tooling can interpolate to re-bind monotonic events to wall-clock time.
- A `Clock_id` enum + compile-time domain declaration so a component can report its clock domain without per-record tags.
- Full serialization surfaces (`to_string`, `to_json`/`from_json`, `std::formatter`) on every new type, per the project mandates.

**Non-Goals:**
- PTP/TSN/NTP monitoring — no reading of `ptp4l`/`chronyd`/`/dev/ptp*` state. `boottime` is reserved in `Clock_id` but not snapshotted.
- Wiring into `app::Infrastructure` / `App_settings` — deferred to a follow-up change.
- Per-record time tags (`Time_point { Clock_id; uint64 ns }`): rejected because it doubles record size on the hot path.

## Decisions

### D1 — Canonical monotonic-raw domain, offset derived at the edge

All events remain stamped in `CLOCK_MONOTONIC_RAW`; the wall clock is never read on a hot path. The snapshot records the *offset* `realtime − monotonic_raw` rather than the raw `realtime` value, matching the existing `sample_utc_offset`/`utc_from_mono_ns` convention (`int64` offset, monotonic `uint64` key).

*Alternatives considered:* storing raw `realtime` and `boottime` values — rejected for the minimal version because only the offset is needed for re-binding, and the offset form is already the established chrono vocabulary.

### D2 — Service lives directly in `nexigen::chrono`

No new namespace. The service types (`ChronoProvider`, `ChronoSink`, `Chrono_snapshot`, `Chrono_settings`, `Clock_id`) sit alongside the existing value types in `include/nexigen/chrono/`, `src/chrono/`, `tests/chrono/`. The directory mapping in `openspec/config.yaml` already covers `nexigen::chrono`, so no config change is required.

*Alternatives considered:* a new `nexigen::time` namespace — rejected by the user; chrono already owns time.

### D3 — Snapshot record is a 16-byte value type

```
Chrono_snapshot {
  chrono::Timestamp stamp;             // 8 B, monotonic-raw series key
  std::int64_t      realtime_offset_ns; // 8 B, signed (realtime − mono)
}
static_assert(std::is_trivially_copyable_v<Chrono_snapshot>);
```

No padding beyond natural alignment (8-byte fields), no tag, no atomic. This stays a cold-path/persisted value, never a hot-path stamp.

### D4 — Single self-pinning background worker

`ChronoProvider` owns one `std::jthread`. The worker pins itself as its first act, exactly like the tracing/metrics workers: `std::jthread` creation is asynchronous, so pinning must happen inside the worker body, not from the spawning thread. `nexigen::chrono::detail::affinity.hpp` provides `pin_self`/`self_affinity`, a module-independent copy of the logging/metrics/tracing helpers (it does not consume them).

Worker loop: `while (!token.stop_requested()) { sleep_until(next); snapshot_now(); next += collect_interval; }`, with a bounded backoff on idle so a pinned core is not burned. `collect_interval == 0` disables the loop; `snapshot_now()` remains the manual trigger.

### D5 — Concurrency and memory layout

The only shared state is the startup/mask reporting surface, made visible to the control plane through relaxed atomics on cache-line-separated cells:

```
alignas(64) std::atomic<std::uint64_t> worker_mask_{};      // worker writes relaxed after self_affinity()
alignas(64) std::atomic<core::ErrorCode> startup_error_{};  // worker writes; startup_status() reads acquire
```

`alignas(64)` keeps these cells off neighbouring hot lines (false-sharing prevention, mandate 5). `snapshot_now()` takes `snapshot_mutex_`, so a manual caller and the background worker serialize their delivery to the sink — the sink is never written by two threads at once (mirroring `MetricProvider::collect_now`). `flush()` is called only from the control plane after the worker is joined (in `shutdown()`). The sink writes use a raw `::write` with a pre-allocated scratch buffer — no heap allocation after construction.

### D6 — Offset sampling skew

One snapshot reads `CLOCK_MONOTONIC_RAW` then `CLOCK_REALTIME` sequentially (two vDSO reads). The offset therefore carries a two-read latency skew on the order of tens of nanoseconds, identical to the accepted behavior of `sample_utc_offset`. This is documented as the re-binding tolerance; a bracketing refinement (mono before/after) is deferred and can be added without changing the spec.

### D7 — Sink contract mirrors metrics/tracing

`concepts::ChronoSink` requires `on_snapshot(const Chrono_snapshot&) noexcept -> core::Status` and `flush() noexcept -> core::Status` — no virtual dispatch, no allocation. `JsonFileChronoSink` writes one JSON object per line (`{stamp, realtime_offset_ns}`) with a raw `::write`, config `{path, append}`. `ConsoleChronoSink`, `NullChronoSink`, and `CompositeChronoSink<Sinks...>` round out the set, mirroring the metric sinks.

### D8 — Settings loader reuses the logging vocabulary

`Chrono_settings` is produced by `load_chrono_settings(Settings_paths, ...)` with precedence defaults → global → local → environment, and best-effort diagnostics. Environment overrides: `NEXIGEN_CHRONO_COLLECT_INTERVAL_NS`, `NEXIGEN_CHRONO_CORE_MASK`. The merged result applies to `ChronoProvider_config { collect_interval, core_mask }`; the sink's file path is carried by `JsonFileChronoSink_config`, not by the settings loader.

### D9 — Serialization on every new type

`Clock_id` (enum), `ChronoProvider_config`, `Chrono_snapshot`, `Chrono_settings`, `JsonFileChronoSink_config`, and the sinks each implement the mandated `to_string`/`to_json`/`from_json`/`std::formatter` surfaces. Loader-produced and live-object types are `to_json`-only with no `from_json` (matching logging/metrics/tracing precedent).

## Risks / Trade-offs

- [Offset skew within one snapshot] → Documented two-read latency; bracketing is a deferred, spec-compatible refinement.
- [No PTP/TSN/NTP yet] → Out of scope; `Clock_id` and the snapshot shape leave room to add `boottime`/`ptp` columns later without redesign.
- [Single worker bottleneck for file I/O] → Acceptable: the service is cold-path and low-rate (one line per `collect_interval`); the pinned worker isolates I/O from real-time threads.
- [Module character shift: chrono gains a service layer] → New types are clearly control-plane only; the existing hot-path value types are untouched and still allocation-free.

## Open Questions

None — the remaining choices (bracketing, boottime column, app wiring) are deferred scope, not unresolved design.
