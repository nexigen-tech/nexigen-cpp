# Proposal

## Why

The platform has no single monotonic time base. Every subsystem calibrates its own
TSC (`logging`, `tracing`, `diagnostics` each own a `StampSource` bank), every sink
derives UTC from its own sampled offset at write time, and the recorder keeps one
global time domain. As a result, log, metric, trace and recorder streams cannot be
placed on one timeline after the fact, and interface FPGA timers — which will be
PTM-synchronised to the host — have no way to become comparable with host time at
all. Before the first FPGA module arrives, the platform needs one authority that
defines the scale, states its quality, and persists enough history to reconstruct
any event's time later.

## What Changes

- **New mandatory daemon `nexigen-chronod`** — forms the single monotonic scale for
  the system and for every declared hardware timer domain, publishes domain
  coefficients (affine map, uncertainty, lock status, epoch) through a shared-memory
  block, and reports a readiness status. The daemon is required for application
  start; a CPU-only deployment with no hardware modules is a valid configuration.
- **Time-base domains and history** — `CLOCK_MONOTONIC_RAW` is the canonical scale;
  TSC and each FPGA counter are domains mapped into it. Maps are an append-only
  history with native and canonical validity intervals, gap markers and per-domain
  epochs, never a single snapshot.
- **Application client** — applications attach to the daemon instead of constructing
  their own clock authority; they declare the domains they require, treat a daemon
  restart with unchanged epochs as transparent, and fail fast when a required domain
  is lost or its epoch changes. Holdover is bounded at 2 s and used only to record the
  cause.
- **Supervised startup and retry** — the time-base daemon is a declared execution
  dependency; dependents start only after the base is ready, and an application that
  exited because the time base was lost is not retried until the daemon is running
  again.
- **Lossless persistence** — every JSONL record carries its canonical monotonic
  instant (`mono_ns`) and the boot token (`boot`); hardware-origin records carry
  `domain_id`, `epoch_id` and raw ticks. UTC stays a derived, human-facing field.
  The recorder gains a time-base history record and per-channel native domains, so
  post-flight analysis can resolve native time → canonical time → UTC.
- **Hardware events carry native time** — the gateway passes raw FPGA ticks plus
  domain and epoch identifiers; the driver no longer converts ticks to nanoseconds.
- **BREAKING** — `chrono::Provider` is constructed by the daemon only, not per
  application process; `app::Infrastructure` fails to initialize when the daemon is
  absent; `EventHeader`/`HardwareMetadata` hardware time changes from nanoseconds to
  ticks plus domain/epoch; JSONL line schemas gain fields (existing readers must
  tolerate unknown keys); the per-instance `chrono.json` sink configuration and
  per-instance `chrono.jsonl` snapshot file give way to one suite-level daemon.

## Capabilities

### New Capabilities

- `chrono/time-base`: the platform time base as a service — canonical scale, domain
  table, affine coefficient history with validity intervals and epochs, publication
  through shared memory, readiness/status, holdover and fail-fast client contract,
  daemon configuration, and the persistence/resolution contract used by analysis.

### Modified Capabilities

- `chrono/provider`: provider construction is daemon-only; the provider is the
  daemon's sampling engine rather than a per-process authority, and its bound sink set
  includes the time-base publisher.
- `app/application`: initialization attaches to the time-base daemon instead of
  constructing a provider, `chrono()` exposes the client handle, and the stop-reason
  vocabulary gains `time_base_lost`.
- `com/event-system`: hardware event time becomes raw ticks plus `domain_id` and
  `epoch_id`; the gateway preserves them and the driver no longer converts.
- `exec/process-supervision`: the time base is a declared dependency with a readiness
  gate, and an application that exited for `time_base_lost` is not retried until the
  daemon is running again.
- `exec/app-suite`: the manifest carries an optional `time_base` object naming the daemon
  process and the published segment.
- `recorder/raw-storage`: the session records the time-base history and per-channel
  native domains; the session manifest binds the boot epoch.
- `logging/file-logger`: the JSON record schema gains `mono_ns` and `boot`, and UTC is
  derived from the published time base instead of a sink-local calibration.
- `metrics/metric-sinks`: the JSON batch line gains `mono_ns` and `boot`.
- `tracing/trace-sinks`: the span JSON line gains `start_mono_ns`, `end_mono_ns` and
  `boot`.

## Impact

**Code.** New daemon under `tools/nexigen-chronod/`; new client and shared-memory
publication under `include/nexigen/chrono/` and `src/chrono/`; `app/infrastructure.hpp`
attaches instead of constructing; `app/infrastructure_stop_reason.hpp` gains one
enumerator; `com/event_header.hpp`, `hardware_metadata.hpp` and the gateway path change
field semantics; `exec` adds the readiness gate and the retry hold; the recorder adds a
time-base record and manifest fields; three JSONL renderers gain fields.

**Dependencies.** The shared-memory block is POSIX shm in `nexigen::core`/`chrono`, not
iceoryx2, so the time base works with `NEXIGEN_ENABLE_COM=OFF` and `nexigen_recorder`
stays COM-free. `nexigen-chronod` must not use `app::Infrastructure` (that would depend
on itself); it is built on `chrono` and the logger directly.

**Performance and latency.** The producer hot path gains no call: log/trace stamps keep
the calibrated TSC read (~5–10 ns) and now take the coefficient from a per-thread cache
refreshed on generation change, so the per-record cost is unchanged. The daemon samples
at 1 Hz plus on change (two `clock_gettime` calls per sample, no allocation); a
coefficient read costs one relaxed atomic load plus a copy of a fixed-size block.
JSONL output grows by ~26–30 bytes per line (~13% on a 200-byte log line, negligible for
metric batches). Application startup loses the per-subsystem TSC calibration (32 slots ×
2 ms per bank, ~128 ms per process) because the scale is shared.

**Memory and allocation.** One fixed-size shm block per suite (a few KiB, sized for the
maximum domain count); no heap allocation on publication, read, or record paths. The
daemon keeps the existing 4 KiB provider bank; per-process provider banks and their
sinks disappear from applications.

**Compatibility.** Existing JSONL files remain readable (absence of `mono_ns`/`boot`
means the legacy format); existing recorder sessions remain decodable through their
`TimeSync` record with unchanged precision; `EventHeader` field semantics change, which
is a wire-format break coordinated with the FPGA gateway work.
