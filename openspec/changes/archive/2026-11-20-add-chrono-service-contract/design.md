# Design

## Context

See `proposal.md` — Why. The current service is a host-only phase-1 design:
`TimeBaseBlock` is 4224 bytes, rows are 128 bytes with one forward Q20 map,
`CLOCK_MONOTONIC_RAW` is the only system scale, `CLOCK_TAI`/`MONOTONIC`/
`REALTIME` are not domains, publication is 1 Hz, history is change-only, and
`nexigen-chronod` runs as a plain process. This change replaces that contract
instead of extending it.

Constraints:

- `chrono` is L1 and links `core`; it must not depend on `com`.
- No exceptions/RTTI, no heap allocation on hot paths, no mutex on a producer
  path.
- Linux target: `clock_gettime`, `clock_nanosleep`, `adjtimex`, PHC ioctls.
- The project mandate forbids compiler extensions; the initial specification
  asks for 128-bit arithmetic. The design resolves this with a portable
  `detail::int128` and an optional `__int128` fast path.

## Goals / Non-Goals

**Goals:**

- One hub scale, one table, and conversion between every pair of domains.
- Honest PTP/TAI state, flags, and holdover.
- Full 1 Hz service snapshots without duplicating coefficient history.
- Every declared hardware domain handled independently, including legacy
  modules without PCIe PTM.
- Deterministic, allocation-free client conversion.

**Non-Goals:**

- Implementing `ptp4l`/`phc2sys`; they remain external.
- Absolute UTC accuracy or a TAI time service.
- A compatibility reader for the retired block/session formats.
- Per-device firmware correction; the device is a witness.

## Decisions

### D-A. Domain vocabulary: two-level names

`TimeDomainClass` describes the role, `TimeDomain` names the scale:

```cpp
enum class TimeDomainClass : std::uint8_t {
    SystemClock,  // CLOCK_* system scales
    CycleCounter, // host cycle counter (TSC on x86)
    DeviceClock,  // hardware timer / PHC / register device
};

enum class TimeDomain : std::uint16_t {
    MonotonicRaw = 0, // CLOCK_MONOTONIC_RAW, the hub
    Monotonic    = 1, // CLOCK_MONOTONIC
    Tai          = 2, // CLOCK_TAI
    Realtime     = 3, // CLOCK_REALTIME
    HostCounter  = 4, // host cycle counter, TSC on x86
};

inline constexpr std::uint16_t device_domain_base = 16U;
// device id = device_domain_base + declared index (16..47)
```

There is no `Canonical` class or `canonical` name. `ClockId` is removed and
replaced by `TimeDomain`; `clock_domain<T>` becomes `time_domain<T>`.

**Alternative rejected:** one flat `TimeDomain` containing a single `Device`
value. It loses the distinction between a system clock and a hardware instance
and makes diagnostics ambiguous.

### D-B. Bidirectional maps and 128-bit arithmetic

Each row keeps the shared anchor pair and two Q32 scales:

```text
fwd: canonical = c_anchor + mul_shift(native    - n_anchor, scale_fwd_q32, 32)
rev: native    = n_anchor + mul_shift(canonical - c_anchor, scale_rev_q32, 32)
```

`scale_rev_q32` is computed at publish time as
`(1 << 64) / scale_fwd_q32` at Q32 precision. Forward uses `native_delta`,
reverse uses `hub_delta`; both are signed 64-bit deltas converted to 128-bit
before multiplication.

`detail::int128` is a small portable value type:

- `__SIZEOF_INT128__` exists: `using int128 = __int128` behind a detail alias.
- otherwise: 64x64->128 via 32-bit halves, with `mul_shift` and `div` helpers.
- No public API exposes a compiler-specific type; the hot path is one
  multiply/shift plus one add.

**Alternative rejected:** storing reciprocal maps as separate rows; it doubles
the table and creates a second validity source that can disagree.

### D-C. Block layout (single contract, no v1 reader)

`TimeBaseBlock` stays `alignas(64)`, one writer, seqlock protected. Header:

```text
offset  size  field
0       8     magic "NXTB"
8       4     layout version (guard only; no legacy branch)
12      4     capacity (32)
16      1     TimeBaseStatus
17      1     SyncMode
18      2     reserved
20      4     ServiceFlags
24      8     generation (seqlock word)
32      8     boot_token
40      8     tai_offset_ns (TAI - UTC)
48      8     utc_offset_ns (hub -> UTC)
56      8     utc_unc_ns
64      8     heartbeat_ns (own cache line)
72      56    reserved
128     4096  rows[32] (128 bytes each)
4224    896   last_snapshot (TimeSnapshot)
5120          total
```

`DomainCoefficient` remains 128 bytes:

```text
0    2  domain_id
2    1  TimeDomainClass
3    1  LockState
4    2  flags (has_reverse, latched, ptm_capable, manual_pair)
6    2  reserved
8    8  epoch
16   8  native_anchor
24   8  canonical_anchor_ns
32   8  scale_fwd_q32
40   8  scale_rev_q32
48   8  unc_ns
56   8  valid_from_native
64   8  valid_to_native
72   8  valid_from_canonical
80   8  valid_to_canonical
88   16 domain_name[16]
104  24 reserved
```

The snapshot is part of the same block so `last_snapshot()` needs no second
mapping. A 1 Hz writer updates it; the coefficient table changes at 10 Hz.

**Concurrency:** generation writes use release ordering (odd while writing,
even when stable); reads use acquire. The reader copies the header before the
generation word, the rows and snapshot around it, then re-reads generation and
accepts only an unchanged even value. The heartbeat is a relaxed atomic on its
own cache line. The writer runs only on the daemon thread; readers never block
it.

**Alternative rejected:** two shared-memory segments (map + snapshot). It adds
a second lifecycle, a second ownership rule, and a cross-segment consistency
problem.

### D-D. 10 Hz publication and 2-state Kalman

Default `publish_interval` becomes 100 ms. Every interval:

1. Sample `MONOTONIC_RAW`, `MONOTONIC`, `TAI`, `REALTIME`, and `adjtimex`.
2. Feed TAI and REALTIME offsets into a fixed-point 2-state Kalman filter
   (state: offset, drift; 2x2 covariance) with a configurable process/measurement
   noise. `MONOTONIC` uses the same filter shape.
3. Sample every declared device pair; feed device estimators.
4. Re-anchor rows at the sample instant, compute reverse scales, update the
   table only when a change exceeds threshold, append history for changes.
5. Publish the block under the seqlock.

The Kalman filter is fixed-size, allocation-free, and runs on the daemon thread
only. The device estimator keeps the current min-residual endpoint fit; PCIe
read latency is one-sided and the filter is not used for device pairs.

**Alternative rejected:** full double-precision matrix Kalman. It adds soft-float
state and code size for a 10 Hz cold loop; fixed-point scalar math is sufficient
for offset/drift and stays deterministic.

### D-E. PTP/TAI state and service flags

`ptp4l` and `phc2sys` are external. `chronod` reads `CLOCK_TAI`,
`CLOCK_REALTIME`, `CLOCK_MONOTONIC`, and `adjtimex` and derives:

- `Standalone`: no PTP evidence; local oscillator/RTC master.
- `PtpSynced`: `CLOCK_TAI` is disciplined and step/slew checks pass.
- `Holdover`: PTP lock was lost; TAI uses the last drift model for a bounded
  window, then becomes `Standalone`/`Degraded`.

`ServiceFlags` bits are grouped by meaning:

```text
presence:     phc_present, tai_available, host_counter_available,
              host_counter_synchronized
expectation:  ptp4l_required, phc2sys_required
health:       ptp_stack_ready, tai_synchronized, step_detected, slew_active
runtime:      rt_sched_applied, mlock_applied
coverage:     declared_domains_complete
```

Flags reflect observed kernel/device state, not process presence; process
presence stays a deployment concern. `SyncMode::Holdover` is the single
holdover indicator, so there is no separate `holdover_active` bit.

`TimeSyncMonitor` is wired into the 10 Hz sample and classifies UTC transitions;
its verdict feeds `ServiceFlags` and the `Realtime` row lock state.

### D-F. Hardware domain access

`DeviceAccessMode`:

```text
PhcCrossTimestamp  // PTP_SYS_OFFSET_PRECISE on /dev/ptpN
RegisterPair       // firmware D1-D3 latched pair, sequence/CRC
ManualPair         // legacy module without PTM/latch
```

`ManualPair` reads the device counter and `CLOCK_MONOTONIC_RAW` in software,
with the read window recorded as latency. The estimator treats the pair as
one-sided and publishes a larger `unc_ns`; the row flags mark `manual_pair` and
`ptm_capable=false`. No driver or gateway converts ticks.

Each declared domain gets its own backend, estimator, epoch, row, and history.
A missing declared domain produces a finding and `Degraded`; a host-only
configuration is valid.

### D-G. 1 Hz snapshots and recorder

`TimeSnapshot` contains generation, boot token, status, sync mode, service
flags, system instants, TAI/UTC offsets and uncertainty, and every domain's
id/class/epoch/value/lock/flags. It does not duplicate the coefficient table;
`generation` selects the history rows valid at that instant.

Writers:

- `chronod` writes one `TimeSnapshot` line per second to `snapshots.jsonl`.
- `chrono.jsonl` remains change-only.
- the recorder stores a `TimeSnapshot` record (new record type) and the new
  `TimeBasePayload` table.
- `nexigen-export` resolves any domain through history + snapshots; gaps remain
  unresolved.

**Alternative rejected:** full coefficient table every second (~4 KiB/s,
~345 MiB/day) without new information.

### D-H. Client and daemon execution

`TimeBaseClient` exposes:

```cpp
struct DomainInstant { std::uint16_t domain_id; std::uint64_t epoch; std::uint64_t value; };

std::optional<DomainInstant> convert(DomainInstant from, std::uint16_t to_domain) noexcept;
std::optional<DomainInstant> convert(DomainInstant from, std::uint16_t to_domain,
                                     std::uint64_t to_epoch) noexcept;
std::optional<TimeSnapshot> last_snapshot() noexcept;
```

`utc_from_canonical` remains an alias for `convert(hub, Realtime)`.
`clock_nanosleep(CLOCK_TAI, TIMER_ABSTIME)` helpers convert a TAI deadline to
the kernel clock without touching the hub.

`chronod` applies a `core::ThreadSpec`: `SCHED_FIFO`, optional affinity,
`mlockall(MCL_CURRENT|MCL_FUTURE)` best effort, and absolute sleep deadlines.
A required attribute failure fails startup; best-effort results are flags.

### D-I. Migration: replace, do not version

The old `TimeBaseBlock`, `DomainCoefficient`, `TimeBasePayload`, `ClockId`, and
their readers are deleted. `chrono.jsonl` from the old service and old `.nxrec`
sessions are rejected as unsupported; there is no offline converter. New
schemas are the only supported ones. JSONL telemetry fields `mono_ns`/`boot`
stay unchanged; sinks use `TimeDomain::Realtime` for UTC.

## Risks / Trade-offs

- **PTP state is inferred from kernel state** → Mitigation: expose flags and
  keep `Standalone` fully functional; external service supervision stays out of
  chronod.
- **Manual pair has larger latency/uncertainty** → Mitigation: record the read
  window, one-sided estimator, per-domain `unc_ns`, and `manual_pair` flag.
- **Q32 reverse scale and 128-bit portability** → Mitigation: portable
  `detail::int128` fallback, compile-time checks, Q32 re-anchor at every
  publication.
- **10 Hz publication cost** → Mitigation: two to five clock reads and one
  table update per 100 ms; no allocation, no locks; history only on change.
- **Snapshot stream volume** → Mitigation: compact snapshot (~1 KiB/s), no row
  duplication, change-only coefficients.
- **Block ABI break** → Mitigation: no dual layout; all consumers are rebuilt
  together; old sessions are explicitly unsupported.
- **PTP hardware absent in CI** → Mitigation: simulated PHC/register devices,
  `Standalone` tests, and fake `adjtimex`/clock sources.
- **TAI availability in containers** → Mitigation: `tai_available=false`,
  `Standalone` mode; TAI-required clients fail fast only when they declared it.

## Migration Plan

1. Replace the vocabulary and block layout; update serialization and tests.
2. Add system-scale sampling, 10 Hz publication, Kalman filters, flags, and
   PTP/TAI state.
3. Wire declared device domains and all three access modes, including manual
   pairs; extend the simulator.
4. Add snapshots, recorder record/schema, exporter resolution, and client
   conversion in one contract step.
5. Apply the daemon RT execution contract and update docs/traceability.
6. Remove legacy `ClockId`, v1 payloads, and old readers.

Rollback is by reverting the change; there is no mixed-version runtime.
