# Proposal

## Why

`nexigen::chrono` today provides only value types and functions (`Timestamp`, `Stamp_source`, `Civil_time`) plus a one-shot UTC-offset snapshot (`sample_utc_offset`). Re-binding a monotonic-stamped event to wall-clock time after the fact — needed for post-mortem correlation of HIL runs — requires the offset as a periodic history, not a single sample. This change adds a small clock-service layer to `nexigen::chrono` that records that history to a file, reusing the provider/sink/settings skeleton already established by logging, metrics, and tracing.

## What Changes

- Add `nexigen::chrono::Clock_id`, a closed enum of base clock domains (`monotonic_raw`, `boottime`, `realtime`) with a compile-time domain declaration and the mandated `to_string`/`from_string`/`to_json`/`from_json`/`std::formatter` surfaces.
- Add `nexigen::chrono::ChronoProvider`, a service that runs one background snapshot worker (CPU-pinned via `core_mask`, like the logging/metrics/tracing workers) which samples the base clock domains every configured `collect_interval` and delivers a `Chrono_snapshot` to a bound sink. `boottime` is reserved in `Clock_id` but not snapshotted in this minimal version.
- Add the `ChronoSink` concept and concrete sinks (console, json-file, null, composite), following the metrics/tracing sink patterns.
- Add `nexigen::chrono::Chrono_snapshot`, the persisted record: a `stamp` (monotonic-raw nanoseconds, the series key) and a signed UTC offset (`realtime` minus `monotonic_raw`), with the mandated serialization surfaces.
- Add `nexigen::chrono::Chrono_settings` and its loader (explicit global/local JSON plus environment overrides), with a small module-local path/diagnostic vocabulary mirroring logging's.

## Capabilities

### New Capabilities
- `chrono/clock-id`: the base clock-domain identifiers and a per-subsystem compile-time domain declaration.
- `chrono/chrono-provider`: the clock service — base-clock sampling, background snapshot worker, CPU affinity, and control plane.
- `chrono/chrono-sink`: the sink abstraction and concrete sinks for snapshot delivery and persistence.
- `chrono/chrono-snapshot`: the snapshot record type and its serialization.
- `chrono/chrono-settings`: settings loading (explicit JSON + environment) for the provider.

### Modified Capabilities

<!-- None: existing hot-path chrono types (Timestamp, Stamp_source) and the
     logging/metrics/tracing subsystems are unchanged. Wiring the service into
     app::Infrastructure / App_settings is deferred to a follow-up change. -->

## Impact

- New files under `include/nexigen/chrono/`, `src/chrono/`, `tests/chrono/`; no change to existing hot-path types (`Timestamp`, `Stamp_source`) or to logging/metrics/tracing producers.
- Performance/latency: zero added latency or allocation on the existing producer hot paths — the per-thread `Stamp_source` path is untouched. The snapshot worker is a cold-path background thread that performs a few clock reads (vDSO) and file I/O once per `collect_interval`.
- Memory allocation: no heap allocation on the steady-state snapshot path; provider storage is pre-allocated during initialization, matching the existing provider/sink skeleton.
- Wiring into `app::Infrastructure` / `App_settings` (a `chrono_` member, `Policy::emplace_chrono_sink`, `Chrono_settings` in `App_settings`) is a follow-up and is intentionally out of scope here.
