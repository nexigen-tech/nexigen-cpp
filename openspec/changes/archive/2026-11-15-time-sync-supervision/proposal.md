# Proposal

## Why

`RS_SAF_10042` requires a mechanism to detect time-synchronization violations. The
platform has monotonic, realtime, and calibrated UTC clocks, but nothing watches
whether the wall clock is being stepped, is moving backwards, or is slewing faster
than the deployment allows. A silently stepping `CLOCK_REALTIME` corrupts every
timestamped sample and every cross-machine deadline calculation.

## What Changes

- A new `chrono::TimeSyncMonitor` samples `CLOCK_REALTIME` and
  `CLOCK_MONOTONIC_RAW` and classifies the offset change as `Ok`, `Stepped`,
  `GoingBackwards`, or `RateExceeded`.
- A `TimeSyncPolicy` carries the thresholds: maximum step, maximum slew rate in
  ppm, and a minimum window below which the rate is not evaluated.
- Classification is a pure function (`classify`) so it is testable without
  manipulating the system clock; `sample()` is the clock-backed wrapper.
- Diagnostics: a `to_string`/formatter for the status and the sample.
- `RS_SAF_10042` moves to `supported` in the matrix.

## Capabilities

### New Capabilities

- `chrono/time-sync`: detects steps, backwards jumps, and excessive slew of the
  realtime clock relative to the monotonic clock.

### Modified Capabilities

None.

## Impact

- **Code**: new `chrono/time_sync_*` headers and `src/chrono/time_sync_monitor.cpp`.
- **Performance**: two `clock_gettime` calls per sample on a caller-driven
  schedule; no allocation, no thread, no blocking.
- **Memory**: one policy plus two previous samples per monitor.
- **Compatibility**: additive; nothing samples it unless the application does.
