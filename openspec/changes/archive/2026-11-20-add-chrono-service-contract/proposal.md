# Proposal

## Why

The current time-base service is a host-only phase-1 design: one
`CLOCK_MONOTONIC_RAW` hub, an optional TSC row, forward maps only, a single
UTC offset, and change-only history. The ChronoService specification requires a
single time space spanning `CLOCK_MONOTONIC_RAW`, `CLOCK_MONOTONIC`,
`CLOCK_TAI`, `CLOCK_REALTIME`, host TSC, and multiple FPGA timers; bidirectional
conversion between every pair of domains; PTP/TAI state with holdover; and
1 Hz full snapshots of the service state for post-flight analysis.

Keeping both contracts would preserve an ABI and a mental model that no longer
match the product. This change replaces the current time-base contract
outright. No compatibility layer, no dual block layouts, no legacy session
reader.

## What Changes

- **BREAKING** Replace the domain vocabulary with a two-level model:
  `TimeDomainClass` (`SystemClock`, `CycleCounter`, `DeviceClock`) and
  `TimeDomain` (`MonotonicRaw`, `Monotonic`, `Tai`, `Realtime`, `HostCounter`,
  device ids).
  `CLOCK_MONOTONIC_RAW` is the hub; the `Canonical` kind/name is removed.
- **BREAKING** Rewrite `TimeBaseBlock` and `DomainCoefficient` for the new
  contract: well-known ids, fixed domain names, forward and reverse anchored
  maps, `SyncMode`, `ServiceFlags`, and the last `TimeSnapshot`.
- Add `CLOCK_MONOTONIC`, `CLOCK_TAI`, and `CLOCK_REALTIME` as ordinary mapped
  domains. Every domain maps to and from `CLOCK_MONOTONIC_RAW`; any-to-any
  conversion is a two-hop operation through the hub.
- Add PTP/TAI state: `Standalone`, `PtpSynced`, `Holdover`, with explicit master
  clock documentation and service-presence/synchronisation flags. `ptp4l` and
  `phc2sys` stay external system services.
- Publish at 10 Hz, with a 2-state fixed-point Kalman filter for the disciplined
  system scales and the existing min-residual estimator for hardware pairs.
- Produce a 1 Hz `TimeSnapshot` containing every domain value, epoch, lock
  state, service flags, offsets, and per-device ticks. Store the snapshot in
  the block, a dedicated snapshots stream, and a recorder record without
  duplicating the coefficient table.
- Process every declared hardware domain independently: PHC cross-timestamp,
  latched register pair, or a manual pair for older modules without PCIe PTM
  (read device counter and `MONOTONIC_RAW` manually). Declared-but-absent
  domains degrade the base and are reported as findings.
- Add bidirectional client conversion (`DomainInstant` from/to any domain), TAI
  absolute-deadline helpers, and `last_snapshot()`.
- Run the daemon with an explicit real-time execution contract: `ThreadSpec`,
  `SCHED_FIFO`, `mlockall` best effort, absolute sleep cadence.

## Capabilities

### New Capabilities

- `chrono/time-domain`: the platform-wide time-domain vocabulary: classes,
  well-known ids, names, epochs, domain declarations, and serialization.

### Modified Capabilities

- `chrono/time-base`: hub semantics, bidirectional domain rows, any-to-any
  conversion, 10 Hz publication, 1 Hz snapshots, multi-device hardware domains,
  service flags, client conversion API, and the removal of the v1 block and
  `Canonical` vocabulary.
- `chrono/clock-id`: remove the legacy `ClockId` vocabulary now superseded by
  `chrono/time-domain`.
- `chrono/time-sync`: add `SyncMode`, PTP/TAI sources, master-clock semantics,
  service flags, step detection, and holdover transitions.
- `recorder/raw-storage`: replace `TimeBasePayload` with the new row layout,
  add the `TimeSnapshot` record type, and bind channels to the new domain ids.

## Impact

**Code.** `include/nexigen/chrono/`, `src/chrono/`,
`tools/nexigen-chronod/`, `include/nexigen/recorder/`, `src/recorder/`,
`tools/nexigen-export/`, `include/nexigen/com/`, `src/com/`,
`include/nexigen/app/infrastructure.hpp`, `src/exec/`, tests, examples, and
`docs/`.

**Breaking compatibility.** Existing `chrono.jsonl` files and `.nxrec`
sessions are not readable by the new runtime. There is no offline converter:
the archive is empty today, so the old contract is retired rather than
migrated. `ClockId`, `Canonical`, the v1 block layout, and the old recorder
payload disappear.

**Dependencies.** No new third-party libraries. PTP/TAI use kernel syscalls
(`clock_gettime`, `clock_nanosleep`, `adjtimex`) and PHC ioctls; `ptp4l` and
`phc2sys` remain external. Older modules use a vendor register/character device
and a software-read pair.

**Performance and latency.** Publication rises from 1 Hz to 10 Hz (two clock
reads plus one table update per interval). A hot client conversion stays pure
integer arithmetic: forward or reverse anchored map, with a portable
`detail::int128` 64x64->128 helper and an `__int128` fast path under
`__SIZEOF_INT128__`. No heap allocation, no syscall, no wall-clock read on the
consumer path. Target conversion cost remains in the low tens of nanoseconds.

**Memory.** One fixed shared-memory block, target at most 8 KiB including the
last snapshot. The 1 Hz snapshot stream adds roughly 1 KiB/s per suite, not a
second copy of the 32-row coefficient table. The recorder adds one snapshot
record per second and one history record per map change.
