# Design

## Context

See `proposal.md` and the `chrono/time-base` delta. The failure is a sampling
defect, not a contract defect: the publisher's certification rule (all CPUs
agree within 50 µs) is sound, but the probe measures that rule with one
unprotected read pair per CPU, so scheduler delay is charged to the counter.

Measured on this machine under `ctest -j16` load, one failure in 300 smoke
iterations:

```
cpu=53 offset=54960 ns, reference 3967 ns, spread 50993 ns, tolerance 50000 ns
```

The offsets of a coherent TSC are not zero: the canonical scale is
`CLOCK_MONOTONIC_RAW`, which the kernel anchors to the TSC with its own offset.
Only the *spread* between CPUs is meaningful, which is why the probe compares
offsets rather than their magnitudes.

## Goals / Non-Goals

**Goals:**

- A coherent TSC is certified whether or not the machine is busy.
- The probe leaves the calling thread's affinity exactly as it found it.
- The certification still rejects a genuinely incoherent counter.
- No publication, conversion, or hot-path change; the probe stays startup-only.

**Non-Goals:**

- Changing the 50 µs tolerance or the `Degraded` outcome.
- Changing how the counter scale is published (`measure_counter_scale` keeps its
  contract; only the probe's internal estimate becomes robust).
- Making `nexigen_chronod_smoke` retry harder or accept `Degraded`; the daemon
  should publish `Ready` on a healthy machine.

## Decisions

### D1 — Bracket each counter read with two canonical reads

```cpp
const auto before = Timestamp::now();   // Result checked
const std::uint64_t ticks = __rdtsc();
const auto after  = Timestamp::now();   // Result checked
mid = before + (after - before) / 2;
span = after - before;
```

A delay of `d` spent inside the pair shifts the implied canonical instant by at
most `d/2`, because the midpoint is used. The sample carries its own error
bound, and the probe compares offsets whose error is bounded by half the span.
This is the smallest change that removes the failure mode: the old code's error
was the *full* delay of one read pair.

*Alternative:* read the counter twice around one canonical read. Symmetric, but
the canonical read is the slower side (vDSO) and would dominate the span.

### D2 — Keep the tightest of a bounded number of attempts

Each sampling site takes up to 16 attempts and keeps the sample with the
smallest span, stopping early once a span of 5 µs or less is found. 5 µs is ten
times tighter than the 50 µs tolerance, so an accepted sample can never consume
more than 10 % of the tolerance budget. Under no load the first attempt wins; on
a busy machine an attempt whose midpoint was disturbed is simply retried.

The same helper produces the two endpoints of the scale estimate (2 ms apart),
so a preemption there cannot bias the scale either — the failure mode that would
otherwise show up as a drift of the offsets across the scan.

*Alternative:* enlarge the tolerance to absorb jitter. Rejected: it would weaken
the certification for real skew (independent counters differ by milliseconds,
but a partially synchronized cluster can differ by tens of microseconds).

### D3 — Restore the caller's affinity with a scope guard

`sched_getaffinity` is snapshotted before the scan and restored by an
object whose destructor runs on every exit path, including the early returns
for a failed clock read. `nexigen-chronod` applies its configured affinity
before the probe, so today the probe overwrites the daemon's own setting; the
spec (`chrono/time-base`, "Real-time daemon execution") requires the applied
affinity to be the one that is reported. Failing to snapshot the mask leaves the
scan's behaviour unchanged: the probe still pins per CPU and still skips CPUs
whose affinity cannot be set.

### D4 — Deterministic regression test through the fault-injection seam

The probe has no injectable clock, so a deterministic regression test needs a
seam. `core::fault_injection` already exists for exactly this purpose ("only
control-plane syscalls consult these points"), and is compiled out of production
builds. Add `FaultPoint::Chrono_tsc_probe_stall`; when a test hook reports it,
the sampler sleeps 200 µs between its bracketed reads.

The test installs a one-shot hook, so the first sample attempt is perturbed and
every later attempt is clean:

- against a single-sample implementation the perturbed sample is used and the
  probe reports skew (51 µs-equivalent spread > 50 µs tolerance) — red;
- against D1+D2 the attempt is retried and the counter is certified — green.

The test skips itself when the machine's counter is not certifiable in the first
place (non-`tsc` clocksource, or a genuinely incoherent counter), and when the
build has fault injection compiled out.

*Alternative:* a load-based test that spawns spinning threads around the probe.
Rejected as non-deterministic: it would be another flake.

## Risks / Trade-offs

- **Cost**: up to 16 × 64 extra `clock_gettime` pairs in the worst case, at
  startup only; each pair is tens of nanoseconds, and the loop exits on the
  first tight sample.
- **False certification**: a CPU whose every attempt is perturbed by more than
  50 µs would be compared with an offset error up to half that span. The
  tolerance is 10× the early-exit budget, and 16 attempts make a sustained
  >5 µs perturbation on all of them vanishingly unlikely; a genuinely skewed
  counter is rejected long before that.
- **Fault-injection point**: one enumerator in a test-only detail header;
  production builds constant-fold the check away, as with the existing points.
