# Proposal

## Why

`nexigen_chronod_smoke` fails intermittently when the suite runs in parallel. The
failure is not in the smoke script: the live daemon publishes `Degraded` from its
first publication and never recovers, so the probe that attaches through the
public client never sees a `Ready` base.

`probe_tsc_synchronized()` (the cross-core certification that lets the publisher
emit the `host_counter` row) takes exactly **one** unprotected sample per CPU:

```cpp
const std::uint64_t ticks = __rdtsc();
const core::Result<Timestamp> now = Timestamp::now();
```

Any scheduling delay between those two reads is indistinguishable from counter
skew. The probe compares the resulting offsets against a 50 µs tolerance, so a
single preemption larger than that on any of 64 CPUs marks a perfectly coherent
TSC as unsynchronized, which pins the publisher to `Degraded` for the whole
daemon lifetime.

Reproduced on this machine with the real smoke test under `ctest -j16` load:
one failure in 300 iterations, with the instrumented probe reporting

```
fail: cpu=53 offset=54960 ref=3967 spread=50993   (tolerance 50000)
```

A 51 µs spread against a 50 µs tolerance — the sample was perturbed, the counter
was fine. The same run's scale estimates varied by 0.06 %, far below what would
be needed to explain a 51 µs offset, so the scale estimate is not the cause.

The same function has a second defect: it pins the calling thread to every CPU
in turn and never restores the original mask. `nexigen-chronod` applies its
configured affinity through `apply_daemon_runtime()` **before** the probe runs,
so the probe silently leaves the daemon's main thread pinned to the last CPU.
`tests/chrono/time_base_test.cpp` works around this by restoring the mask by
hand and says so in a comment.

## What Changes

- **Bracket every counter read in the probe.** Each sample reads the canonical
  clock before and after `__rdtsc()` and uses the midpoint, so a stall inside
  the pair can only contribute half its span as offset error. The probe keeps
  the tightest of a bounded number of attempts per CPU (and per scale endpoint)
  instead of a single unprotected read.
- **Restore the caller's affinity.** A scope guard snapshots the affinity mask
  before the scan and restores it on every exit path, so the daemon keeps the
  affinity it applied and callers are not left pinned.
- **Keep the certification semantics.** The 50 µs tolerance, the "skip CPUs
  whose affinity cannot be set" rule, and the `Degraded` outcome for a genuinely
  incoherent counter are unchanged.
- **Test-only stall injection.** Add `FaultPoint::Chrono_tsc_probe_stall`, a
  cold-path fault-injection point that stalls 200 µs inside a sample, so a test
  can prove that jitter is retried rather than reported as skew. Production
  builds compile the check to `false` exactly as the existing points do.

No publication, conversion, or hot-path change; the probe stays startup-only.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `chrono/time-base`: the host-counter certification samples the canonical clock
  on both sides of each counter read, retries jittered samples, certifies a
  coherent counter under load, still rejects a counter whose CPUs disagree by
  more than the tolerance, and restores the calling thread's affinity.

## Impact

- **Code**: `src/chrono/time_base_host_domain.cpp` (probe and new affinity
  scope); `include/nexigen/core/detail/fault_injection.hpp` (one enumerator).
- **Tests**: `tests/chrono/time_base_test.cpp` — affinity preservation is
  asserted instead of repaired by the test, and a fault-injected stall must not
  uncertify the counter.
- **Docs**: `docs/adr/0001-time-source-policy.md` gets a note on why the probe
  brackets its reads; `TODO.md` records the flake and its resolution.
- **Performance**: startup-only. The probe adds up to 16 bracketed attempts per
  CPU instead of one read pair; each attempt is two vDSO `clock_gettime` calls
  and costs tens of nanoseconds, and the loop stops at the first tight sample.
- **Compatibility**: no API change. `probe_tsc_synchronized()` keeps its
  signature and its `bool` verdict.
