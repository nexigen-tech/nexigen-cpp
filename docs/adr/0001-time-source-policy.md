# ADR 0001 — Time source policy for telemetry and recorded data

- **Status:** Accepted
- **Date:** 2026-11-21
- **Scope:** `nexigen::chrono`, `nexigen::logging`, `nexigen::metrics`, `nexigen::tracing`, `nexigen::recorder`, `nexigen::app`

## Context

Every telemetry subsystem and the recorder need one comparable notion of "when". Today the pieces exist separately:

- `chrono::Timestamp::now()` reads `CLOCK_MONOTONIC_RAW` through `clock_gettime` (`include/nexigen/chrono/timestamp.hpp:61-63`).
- `chrono::StampSource` converts `__rdtsc()` ticks into that same domain through a per-producer calibration pair, re-anchoring roughly every 24 minutes; on non-x86 it falls back to `Timestamp::now()` (`include/nexigen/chrono/stamp_source.hpp`).
- Logging stamps each record on the producer path from a per-slot `StampSource` (`logging/detail/producer.hpp:544`); the consumer synthesizes drop summaries with a fresh `Timestamp::now()` (`producer.hpp:333-349`).
- Tracing stamps span start/end from a per-slot `StampSource` (`tracing/tracer_provider.hpp:458`).
- Metrics carry no per-record time; the harvest stamps the collection window with `Timestamp::now()` and a sampled UTC offset (`src/metrics/provider.cpp:333-344`).
- UTC is derived in two different ways: `chrono::sample_utc_offset` reads `CLOCK_REALTIME − CLOCK_MONOTONIC_RAW` (`src/chrono/utc_offset.cpp`), while sinks and the recorder resolve through the published time base (`TimeBaseClock::utc_from_canonical`, `recorder::TimeBaseResolver`).
- The recorder already treats `TimeDomain::MonotonicRaw` as its hub and resolves TAI/UTC only at export (`docs/recorder.md`).

The project already pays for these choices: accounting and ordering only work because all stamps share one monotonic domain, while wall-clock reads would both break ordering across NTP steps and violate the hot-path budget (a fresh `clock_gettime` alone measures ~20-25 ns, the whole log-enqueue budget).

The gap is that the policy is spread across four artifacts and two runtime paths silently deviate from it. This ADR records the policy once and lists the deviations as migration work.

## Decision

1. **Storage domain.** Every persisted or transported timestamp SHALL be calibrated monotonic nanoseconds in the `CLOCK_MONOTONIC_RAW` domain. Raw TSC ticks SHALL NOT be stored: they are not self-describing, depend on a per-producer calibration pair that re-anchors at runtime, and are not portable to non-x86 targets.

2. **Hot path.** On any recording path, the only permitted time source is `chrono::StampSource::sample()`. No `Timestamp::now()`, no `CLOCK_REALTIME`, no UTC conversion, no syscall, and no allocation.

3. **Cold path.** Control-plane and consumer-side stamping SHALL use `Timestamp::now()` (`CLOCK_MONOTONIC_RAW`). The same domain as the hot path keeps producer and consumer stamps comparable. A dedicated `StampSource` on a cold path is not required. Both sources produce the same domain, epoch, and unit, but not the same value: `StampSource::sample()` is a calibrated model of the clock rather than a read of it, so a sample and a fresh clock read taken at the same instant may differ by nanoseconds and, under drift or an aging anchor, by microseconds. Monotonicity is per source (the `last_ns` clamp), not global: a later stamp from one source can be numerically smaller than an earlier stamp from another. Strict cross-thread ordering SHALL use sequence numbers, not timestamps alone.

4. **Metrics are timeless per record.** Counters, gauges, statistics, and histograms SHALL NOT carry a timestamp. Only a collection window or snapshot is stamped, at harvest time.

5. **UTC is a presentation concern.** During application runtime the system SHALL NOT read `CLOCK_REALTIME`. UTC/TAI SHALL be derived on demand, at the export or render boundary, from the published time base (`TimeBaseClock::utc_from_canonical`, `recorder::TimeBaseResolver`). When the time base is unbound, uncalibrated, or outside its validity horizon, UTC SHALL be omitted and marked unresolved; it SHALL NOT be fabricated.

6. **Raw offset sampling is a fallback.** `chrono::sample_utc_offset` remains valid only for contexts without a published time base: offline tools, standalone daemons that do not attach, tests, and fixtures. It SHALL NOT be used by runtime telemetry providers when a time base is available.

7. **Detach boundary.** `TimeBaseClock::detach()` SHALL run only after the last sink delivery and flush of the shutdown sequence, because sinks resolve UTC through the published time base. Quiescence ordering does not otherwise affect time values.

## Consequences

**Positive**

- One comparable timeline across logging, metrics, tracing, and recorder; ordering survives clock steps.
- No clock syscalls on any recorded path; no new hot-path cost.
- Recorded data stays readable offline: monotonic stamps plus the published calibration describe how to obtain UTC.
- UTC is never invented; consumers can distinguish resolved from unresolved time.

**Negative / accepted**

- Offline analysis must resolve UTC through the session manifest/time-base block rather than reading a stored wall clock.
- A monotonic stamp is meaningless without boot context; sessions must carry the boot token (the recorder already does).
- Cold-path stamping keeps a second clock mechanism (`Timestamp::now()`), which is deliberate.
- Cross-source ordering is approximate. Each source is non-decreasing on its own, but there is no global total order; a consumer that needs strict ordering across producers must use sequence numbers, not timestamps alone.

**Deviations — resolved by `add-telemetry-diagnostics`**

- `MetricBatchSnapshot.utc_ns` now resolves through `TimeBaseClock::utc_from_canonical`
  and carries `utc_resolved`; JSON omits the UTC value when the base cannot resolve the
  window (`metrics/metric-provider`, "Record and batch snapshot serialization").
- `trace_daemon_session` resolves snapshot UTC through the attached base. The offset
  sampled once at session startup is a fallback only, reported on stderr once; the trigger
  path performs no `CLOCK_REALTIME` read (`tracing/trace-daemon`, "Snapshot UTC
  resolution").
- The runtime no-`CLOCK_REALTIME` rule is pinned by the `clock_gettime` interposer in
  `tests/tracing/trace_daemon_test.cpp`.
- The diagnostics channel that reports unresolved time is recorded in
  `docs/adr/0003-telemetry-diagnostics.md`.

## Alternatives considered

- **Store raw TSC ticks in records.** Rejected: records become producer-bound, require shipping a re-anchoring calibration pair, lose the monotonic clamp, and have no meaning on non-x86.
- **Read `CLOCK_REALTIME` per record.** Rejected: two syscalls per record, NTP steps become visible inside a batch, and ordering across records breaks.
- **Stamp records with wall time.** Rejected: non-monotonic across NTP/PTP adjustments; ordering, windows, and retention depend on a monotonic scale.
- **Per-record timestamps in metrics.** Rejected: 8 bytes per record plus a sample on every `inc()`, against a 2 ns recording gate; metrics are cumulative by design.
- **Give every cold consumer its own `StampSource`.** Rejected: no measurable gain on a path without a latency budget, and it adds calibration owners that must not be sampled from foreign threads.

## References

- `openspec/specs/chrono/stamp-source/spec.md` — calibrated source, calibration contract, UTC offset snapshot, monotonic-to-UTC conversion.
- `openspec/specs/chrono/time-base/spec.md` — published series, validity horizon, boot token.
- `docs/recorder.md` — `TimeDomain::MonotonicRaw` hub and UTC/TAI resolution at export.
- `openspec/changes/keep-telemetry-handles-valid-until-infrastructure-destruction/` — quiescence contract that fixes the detach boundary (decision 7).
