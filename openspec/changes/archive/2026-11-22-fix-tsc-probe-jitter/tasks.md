# Tasks

## 1. Robust probe sampling

- [x] 1.1 Replace the single unprotected read pair in `probe_tsc_synchronized()` with a bracketed sample (`now`, `__rdtsc()`, `now`) that reports its own span and uses the midpoint
- [x] 1.2 Sample the tightest of up to 16 attempts per site, stopping at a span of 5 µs; use the same helper for both endpoints of the 2 ms scale estimate
- [x] 1.3 Keep the 50 µs tolerance, the per-CPU pinning, the "skip CPUs whose affinity cannot be set" rule, and the `false` verdict for a failed clock read
- [x] 1.4 Keep the function under the complexity gate by extracting the sampler, scale and comparison helpers (CCN 23 → 5)

## 2. Affinity preservation

- [x] 2.1 Snapshot the calling thread's affinity with `sched_getaffinity` before the scan and restore it on every exit path with a scope guard
- [x] 2.2 Remove the test's manual restore and assert that the mask is unchanged after the probe

## 3. Regression tests

- [x] 3.1 Add `FaultPoint::Chrono_tsc_probe_stall` to `core::detail::fault_injection` and stall 200 µs inside the bracketed sample when it fires
- [x] 3.2 Add a test that installs a one-shot stall hook and asserts the probe still certifies the counter; skip when the counter is not certifiable or injection is compiled out
- [x] 3.3 Add a test that stalls every attempt on one CPU and asserts the probe rejects the counter, proving the retry logic did not widen the tolerance
- [x] 3.4 Prove both new assertions are red before the fix (single-attempt sampler, no affinity restore) and green after it

## 4. Verification

- [x] 4.1 Build and run the chrono suite in Debug with `-DNEXIGEN_ENABLE_FAULT_INJECTION=ON` and verify both injected tests pass
- [x] 4.2 Run the smoke loop from `proposal.md` (real `nexigen_chronod_smoke` under `ctest -j16` load) for 300 iterations and verify zero failures (baseline: 1 failure per 300)
- [x] 4.3 Run the full Debug suite with `ctest -j 64` three times and verify `nexigen_chronod_smoke` passes every time
- [x] 4.4 Run the full fault-injection Debug suite and verify no regressions
- [x] 4.5 Record the flake and its resolution in `TODO.md` and note the bracketing rule in ADR 0001
- [x] 4.6 Run `openspec validate fix-tsc-probe-jitter --type change --strict`

## Workflow follow-up

- Archive the change once review and CI requirements are satisfied.
