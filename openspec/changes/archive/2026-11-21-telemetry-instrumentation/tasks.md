# Tasks

## 1. Logging

- [x] 1.1 Add `M = core::NullMeter` to `detail::Producer` and create `accepted` plus the three
      `dropped.*` counters; verify a `NullMeter` producer has no counter storage.
- [x] 1.2 Add `M` to `ConsoleLogger`, `FileLogger`, and `Iceoryx2Logger`, forward the meter to the
      producer, and verify their tests pass.
- [x] 1.3 Verify the counters match the `dropped()` snapshot after a known drop mix.

## 2. Tracing

- [x] 2.1 Add `M = core::NullMeter` to `TracerProvider` and create `started`,
      `dropped.buffer_full`, and `dropped.slot_exhausted`; leave `dropped.rate_limited` in
      `DroppedStats` only (instruments never leave the tracing component).
- [x] 2.2 Verify the counters match `dropped()` after a known span mix and that a `NullMeter`
      provider stores no counters.

## 3. Chrono

- [x] 3.1 Add `M = core::NullMeter` to `chrono::Provider` and counters `calibration_failed`,
      `acquire_exhausted`, `snapshot_failed`.
- [x] 3.2 Verify each counter advances on its failure path and the returned statuses are unchanged.

## 4. Collector e2e and CI

- [x] 4.1 Extend `tests/tools/recd_smoke.sh` with two application codes, an event stream, and a
      forced sequence gap; verify datasets, gaps, and the event dataset.
- [x] 4.2 Add the `nexigen-recd` COM CI job (build plus smoke) with the pinned iceoryx2 revision.
- [x] 4.3 Run Debug, Release, portable Release, TSan, ASan, and coverage; verify 100% tests pass
      with the module gates unchanged.

## Workflow follow-up

- Archive after review; rebuild the graph with `graphify update .`.
