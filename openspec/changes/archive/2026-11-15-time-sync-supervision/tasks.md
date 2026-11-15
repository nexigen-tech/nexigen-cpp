# Tasks

## 1. Monitor

- [x] 1.1 Add `TimeSyncStatus`, `TimeSyncPolicy`, and `TimeSyncSample` with the full conversion set and diagnostics; verify round-trips and formatter output.
- [x] 1.2 Implement `TimeSyncMonitor::classify` (backwards, stepped, rate with the minimum window) and the clock-backed `sample()`; verify each classification and two consecutive real samples.

## 2. Documentation and verification

- [x] 2.1 Document the monitor in `docs/architecture.md`, promote `RS_SAF_10042` in the matrix, and verify the traceability gate exits 0.
- [x] 2.2 Run `nexigen_chrono_tests`, the complexity gate, and `openspec validate --changes --strict`; verify all pass.

## Workflow follow-up

- Archive the change after review.

## 3. Review follow-up

- [x] 3.1 Evaluate the rate through one shared helper that returns 0 outside the step/window tolerance, so `sample()` and `classify()` agree and a large wall-clock step cannot overflow the ppm product; verify multi-day steps and an unbounded tolerance.

