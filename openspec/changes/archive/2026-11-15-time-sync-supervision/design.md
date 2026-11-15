# Design

## Context

See `proposal.md` — Why. `chrono` already owns monotonic/realtime time points and
the calibrated UTC offset; no component watches the offset for jumps.

## Goals / Non-Goals

**Goals:**

- A pure, testable classification of offset transitions.
- A clock-backed sampler that stays allocation-free and thread-free.
- Thresholds that a deployment can tighten per machine.

**Non-Goals:**

- PTP/NTP configuration or discipline.
- Automatic reaction (abort, reset); the caller decides.
- Detecting monotonic-clock anomalies (monotonic cannot step by contract).

## Decisions

**D1 — Offset = `CLOCK_REALTIME − CLOCK_MONOTONIC_RAW`, caller-driven.** No
background thread: the application (or PHM) samples on its own schedule. This
keeps the monitor usable in tests and avoids another thread with its own affinity
story.
*Alternative rejected:* a chrono worker thread — it would duplicate the existing
worker and hide the sampling cadence.

**D2 — `classify` is a pure function.** Steps are rare and require privileges to
provoke; testing the real clock is not practical. `classify(previous_offset,
current_offset, elapsed, policy)` is the testable core; `sample()` only reads the
clocks and calls it.

**D3 — Backwards before Stepped.** A negative jump beyond the threshold is a
distinct, more severe event (time went backwards), so it is classified first.

**D4 — Rate uses ppm with a minimum window.** `rate_ppm = step × 10⁶ / elapsed`;
below `min_window_ns` the ratio is noise, so only the step check applies.

**D5 — Clock failure yields no sample.** `sample()` returns `std::optional` and
returns `nullopt` when `clock_gettime` fails, rather than inventing an `Ok`.

## Risks / Trade-offs

- **A large NTP correction at startup looks like a step.** → The first sample
  only establishes the baseline; subsequent steps are real for the deployment's
  policy.
- **Thresholds are deployment-specific.** → Defaults are conservative (1 ms,
  500 ppm); the policy is configurable.
- **Slew detection can be noisy on a loaded VM.** → The minimum window filters
  short-term jitter.

## Migration Plan

Additive. A caller samples and reacts; removing the calls restores the previous
behavior.
