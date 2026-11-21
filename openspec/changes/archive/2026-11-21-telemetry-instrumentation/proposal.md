# Proposal

## Why

`com` endpoints now create counters from an injected meter, but loggers, `TracerProvider`, and
`chrono::Provider` still expose only snapshots. The recorder therefore cannot observe their
accepted/dropped/failure counts as metric frames, and the collector e2e/CI coverage is incomplete.

## What Changes

- Add `M = core::NullMeter` to `ConsoleLogger`, `FileLogger`, `Iceoryx2Logger`, and their shared
  `Producer`; create `accepted` and the three `dropped.*` counters.
- Add `M = core::NullMeter` to `TracerProvider`; create `started` and the three `dropped.*`
  counters.
- Add `M = core::NullMeter` to `chrono::Provider`; create `calibration_failed`,
  `acquire_exhausted`, and `snapshot_failed`.
- Extend the collector e2e test to two applications plus events and a forced gap, and add the
  `nexigen-recd` COM CI job.

**BREAKING**: none; every new template parameter defaults to `core::NullMeter`.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `logging/multi-producer`: loggers accept a meter and count accepted and dropped records.
- `tracing/tracer-provider`: the provider accepts a meter and counts started and dropped spans.
- `chrono/provider`: the provider accepts a meter and counts calibration, acquisition, and snapshot
  failures.

## Impact

- **Code**: `include/nexigen/logging/{console,file,iceoryx2}_logger.hpp`,
  `logging/detail/producer.hpp`; `tracing/tracer_provider.hpp`; `chrono/provider.hpp`; tests in those
  modules; `tests/tools/recd_smoke.sh`; `.github/workflows/ci.yml`.
- **Performance**: counters increment once per endpoint event; `NullMeter` compiles them away.
- **Dependencies**: none new; `metrics` remains outside these modules.
