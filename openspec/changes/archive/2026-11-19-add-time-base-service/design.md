# Design

## Context

See `proposal.md` — Why. Current constraints that shape the approach:

- `nexigen::chrono` is L1 and links `nexigen::core` only; `logging`, `metrics`,
  `tracing`, `recorder` and `app` all link it. Depending on `nexigen::com` from
  `chrono` would drag iceoryx2 into every consumer and break `NEXIGEN_ENABLE_COM=OFF`
  and the recorder's COM-free boundary (`openspec/specs/recorder/raw-storage/spec.md`,
  "COM-free storage boundary").
- `CLOCK_MONOTONIC_RAW` is derived from the TSC on the current target (kernel
  `clocksource = tsc`, `constant_tsc`/`nonstop_tsc`), but only as an affine map with a
  boot-time base; it is not the TSC value and the relation is conditional on the kernel
  clocksource.
- PCIe PTM anchors a device to the root complex's PTM time source, not to the TSC;
  the host side of the map therefore has its own edge.
- No tool in `tools/` uses `app::Infrastructure`; daemons are built on the modules
  directly, so a daemon that applications depend on cannot create a bootstrap cycle.

## Goals / Non-Goals

**Goals:**

- One canonical monotonic scale, one owner, one published table of domain maps.
- Cheap reads on producer paths: no lock, no allocation, no syscall.
- Truthful history: any recorded instant resolves later, or is reported unresolved.
- Works with zero hardware modules and with `NEXIGEN_ENABLE_COM=OFF`.
- Deterministic failure: explicit status, bounded holdover, no restart storms.

**Non-Goals:**

- Absolute time-of-day accuracy or a TAI/UTC time service. UTC is derived decoration.
- A network time protocol implementation; IEEE 1588 stays outside the platform start
  sequence per the deployment decision.
- Correcting device counters inside firmware; the device is a witness (D2).
- Hot-path conversion of every hardware event by the daemon; consumers convert.

## Decisions

### D-A. Topology: one daemon per suite, supervised by exec

`nexigen-chronod` is a suite member (`AppSpec` with `type=daemon`, `reporting=true`,
`impact=Failure`). One instance per suite run, named by the suite/run key, so two
parallel suites never share a scale and a suite restart starts a fresh one.

*Alternative rejected:* a platform-wide singleton — it needs a supervisor outside the
suite model, and a crash would take down unrelated suites.

### D-B. Transport: POSIX shared memory + seqlock in `core`, not `com::Field`

The block lives under `/dev/shm` with a name derived from the run key. Rationale: the
time base must exist with COM off, `chrono` must stay L1, and the read path must be a
few loads rather than an iceoryx2 blackboard call.

*Alternatives:* `com::Field` (rejected: layering and COM gate); a file with `mmap`
(rejected: slower attach, no natural single-writer); a per-application pipe (rejected:
does not give last-value-on-connect).

### D-C. Block layout and concurrency

```
offset  size   field                     notes
0       8      magic "NXTB"              u64
8       4      version                   bumps on layout change
12      4      capacity                  maximum domain rows
16      1      status                    TimeBaseStatus (u8)
17      3      padding
20      4      domain_count
24      8      generation                seqlock word (even = stable)
32      8      boot_token                64-bit prefix of /proc boot_id
40      8      utc_offset_ns             canonical -> UTC (i64)
48      8      utc_unc_ns
56      8      utc_valid_to_canonical
64      8      heartbeat                 alignas(64), own cache line
72      56     padding
128     ...    rows[capacity]            each row 128 bytes
```

Row (128 bytes): `domain_id u16`, `kind u8`, `lock u8`, `flags u16`,
`reserved0 u16`, `epoch u64`, `native_anchor u64`, `canonical_anchor_ns i64`,
`scale_q20 u64`, `unc_ns u64`, `valid_from_native u64`, `valid_to_native u64`,
`valid_from_canonical u64`, `valid_to_canonical u64`, `reserved u64[6]`.

The map is evaluated in anchored form,
`canonical = canonical_anchor_ns + ((native - native_anchor) * scale_q20 >> 20)`,
which is the same affine function as `offset + native * scale` but keeps the
64-bit product bounded for the validity horizons the daemon publishes (it
re-anchors at least once per minute, far inside the overflow bound). Capacity 32
→ 4 KiB of rows.

Writer: `generation.fetch_add(1, release)` (odd), write rows, `fetch_add(1, release)`
(even), then `heartbeat.store(mono_ns, relaxed)`. Reader: `generation.load(acquire)`,
retry while odd, copy, re-load and compare; one `relaxed` generation load per hot-path
use decides whether the cached copy must be refreshed. The heartbeat is a relaxed
atomic on its own cache line so a reader can test freshness without copying the table.

*Alternative rejected:* double-buffer with an atomic index — more state to keep
consistent and no benefit at this write rate.

### D-D. Canonical scale and the TSC edge

`MONOTONIC_RAW` is the canonical domain (row identity, `scale=1`, `unc=0`). The TSC is
an ordinary domain whose map the daemon measures; nothing assumes `TSC == MONO_RAW`.
At start the daemon checks `current_clocksource == tsc`; if not, it publishes no TSC row
and reports `Degraded` for consumers that require it. Cross-core TSC synchronisation is
validated with `RDTSCP` + `TSC_AUX` probes on pinned threads and published as a
capability; without it, the TSC domain is not advertised.

*Alternative rejected:* publishing raw TSC ticks as the canonical scale — every existing
`Timestamp` consumer would need a conversion, and the kernel's map would still be in
play.

### D-E. Device contract: pair on one edge, raw counter, no correction

Firmware exposes a free-running device counter and a hardware-latched pair
`(dev_ticks, ptm_ticks)` taken on one edge (read-latch or trigger + seqlock), plus
`LOCK_STATE`, `LOCK_LOST_STICKY`, `SYNC_EPOCH`, `LAST_EXCHANGE_DEV_TICKS`,
`PROP_DELAY_NS`, `RESIDUAL_NS`, and a versioned register map header
(`MAGIC/VERSION/CAPS` + device UUID). The host-side driver does not convert ticks.

Two exposure levels are supported by the same register block:

1. **PHC** — the driver registers a PTP clock with `gettime64`/`gettimex64`, and the
   daemon reads cross-timestamps through `PTP_SYS_OFFSET_PRECISE`/`_EXTENDED`.
2. **Registers** — the daemon reads the pair directly through `UIO`/`VFIO`/the vendor
   character device.

Level 1 is preferred where a kernel driver exists; level 2 is the minimum and keeps the
same daemon-side estimator. `PTM-Unsupported` is a normal `FreeRun` state, not an error.

### D-F. Estimator and publication policy

For each device domain the daemon samples pairs, rejects samples by minimum residual
over a window (PCIe read latency is one-sided), fits `scale` from the window endpoints
and `offset` from the minimum-latency sample, and derives `unc` from the residual
spread. A new history row is published only when the map, epoch, lock state or status
changes beyond thresholds; the heartbeat publishes every interval regardless. Holdover
grows `unc` linearly with elapsed canonical time.

`TimeSyncMonitor` is reused for the canonical→UTC row classification (`Ok`/`Stepped`/
`GoingBackwards`/`RateExceeded`) and its verdict becomes the UTC row's status; it stops
being the platform's only time-supervision mechanism.

### D-G. Client and failure semantics

The client attaches read-only, caches the rows it requires per thread, and refreshes on a
generation change (one relaxed load per use). It declares required domains in its
settings; attach fails when a required domain is missing. A daemon restart with unchanged
required epochs is transparent. Loss of a required domain, an epoch change, or a
heartbeat older than `max(2 s, 3 × publish interval)` starts holdover; at expiry the
application stops its run loop with `InfrastructureStopReason::TimeBaseLost` and exits
with the reserved code `75`, without self-retry.

*Alternative rejected:* marking records written during holdover — the canonical stamps
remain valid and the gap is recorded in the history, so a per-record flag adds format
churn without adding information.

### D-H. Startup and retry gates

`exec` treats `chronod` as a dependency: a dependent starts only after the process is
`Running` **and** the block reports `Ready`. Readiness is read from the block, not from
the COM readiness channel, so the gate works with COM off. An application that exited
with code 75 is held out of the restart loop until readiness returns; the hold does not
consume its restart budget.

The binding is explicit in the suite manifest: an optional `time_base` object names the
daemon process (`process`) and the published segment (`segment`). An absent `process`
disables the gate; `process` without `segment` defaults to
`/nexigen-timebase-<suite_id>`; `segment` without `process` is rejected, as is a
`process` that does not name an application in the same manifest.

### D-I. Persistence and resolution

- Boot token: 16 hex characters = the first 8 bytes of the kernel `boot_id`; the full
  UUID is written in the low-rate history file.
- JSONL streams carry `mono_ns`/`boot` (and `start_mono_ns`/`end_mono_ns` for spans);
  UTC stays a derived field. Absence of the fields means the legacy format.
- The daemon's history file (`chrono.jsonl` stream kind) carries one line per published
  change with native and canonical validity, status, uncertainty and the full boot UUID.
- The recorder gains a `TimeBase` record type (same row layout plus the boot UUID) and
  per-channel native-domain declarations; `TimeSync` stays for compatibility.
- Resolution is three steps: native → canonical through the row valid at the native
  instant; canonical → UTC through the recorded UTC row; gaps are reported unresolved.

### D-J. Settings and test seam

The daemon reads its own settings document: suite/run identity, publish interval,
declared hardware domains (name, UUID, access path), estimator windows. Applications
keep only client settings: required domains and the holdover window. Per-instance
`chrono.json` sink configuration and the per-instance `chrono.jsonl` file are removed;
the daemon owns one history file per suite.

Tests and `Infrastructure` tests use an in-process fake publisher over an anonymous
`MAP_SHARED|MAP_ANONYMOUS` region with the same seqlock and layout, so no daemon process
is needed to exercise attach, refresh, holdover and fail-fast.

## Risks / Trade-offs

- **Stale shm segment after a crash** → the daemon opens with `O_CREAT`, validates magic,
  version and boot token, and takes over only when the recorded owner is gone or the
  heartbeat is stale; a clean shutdown unlinks. Readers treat a frozen heartbeat as loss.
- **TSC not synchronised across sockets** → probe before advertising the TSC domain; when
  it fails, consumers requiring TSC fail fast while MONO_RAW consumers continue.
- **Kernel switches clocksource** → the TSC row is withdrawn and status becomes
  `Degraded`; this is the failure mode that is invisible today.
- **Daemon restart longer than 2 s** → by design applications fail; mitigate with zero
  restart delay, a warm start that only re-opens the segment, and a measured budget.
- **JSONL growth** → ~26–30 bytes per line (~13% on a log line, negligible on a batch);
  history rows are change-only, so the history file stays small.
- **EventHeader wire break** → version the header and change it in the same phase as the
  gateway; until firmware is updated, hardware domains are not advertised.
- **Test surface** → the fake publisher must be the only way tests construct a time base;
  otherwise every app test needs a real daemon.

## Migration Plan

1. **Phase 1 — host-only base, mandatory attach.** Core shm block + client + daemon
   (MONOTONIC_RAW and TSC rows), `Infrastructure` attach, `TimeBaseLost`, exec readiness
   and retry gates, JSONL fields in logs/metrics/traces, history file. Applications stop
   constructing `Provider`.
2. **Phase 2 — durable analysis.** Recorder `TimeBase` record and per-channel domains,
   exporter resolution, hub display of the history stream.
3. **Phase 3 — hardware domains.** `EventHeader` change, gateway passes ticks/domain/
   epoch, daemon device backends (PHC or registers), estimator enabled per declared
   domain.

Rollback: Phase 1 reverts by restoring provider construction in `Infrastructure`; JSONL
fields are additive and old readers ignore them. Phase 3 reverts by not declaring hardware
domains — the base stays `Ready` with host rows only.

## Open Questions

- Whether to publish a `CLOCK_MONOTONIC` (slewed) interop domain for components that
  still wait on `steady_clock`; it changes no requirement and can be added as another
  row later.
- Whether the daemon should drain and rotate its own history file or delegate rotation
  to the logging module; either keeps the published contract unchanged.
