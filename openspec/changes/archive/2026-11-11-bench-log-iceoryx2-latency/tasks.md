# Tasks

## 1. Harness scaffold

- [x] 1.1 Create `tests/com/latency_test.cpp` with the shared rdtsc harness (ns/tick calibration over a 50 ms window, WARMUP 2000, N 10000, best-of-N passes, producer/consumer pinned to distinct allowed cores, zero-allocation assertion) plus the `alignas(64)` trivially-copyable `LogFrame256` stand-in payload. Verify: `nexigen_com_latency_tests` builds in Debug and prints medians for an empty measured window.
- [x] 1.2 Register the `nexigen_com_latency_tests` target in `tests/CMakeLists.txt` behind `NEXIGEN_ENABLE_COM`, wiring the same strict-latency compile definition the existing latency suites use. Verify: `ctest -N` lists `nexigen_com_latency_tests`.

## 2. Measurement variants

- [x] 2.1 Variant (a) direct: measure `loan_uninit() → write_payload(LogFrame256) → send()` and report median/p99/p99.9/max. Verify: stable printed numbers across three consecutive local runs (small band).
- [x] 2.2 Variant (a+event): add `EventNotifier::notify()` to the measured path (the full `REQ_LOGC_005` path over two services). Verify: printed numbers are strictly larger than (a) but the same order of magnitude.
- [x] 2.3 Variant (b) worker-publish: producer `emit` into the existing SPSC ring, a consumer thread runs `pop → loan/write_payload/send/notify`; measure the producer and worker sides separately. Verify: producer median stays near the ~20 ns baseline, worker cost reported separately.

## 3. Verification matrix

- [x] 3.1 Debug gate-ON build: `nexigen_com_latency_tests` runs and prints numbers (no assert), full `ctest` green.
- [x] 3.2 Release `-O3 -march=native -flto` with `NEXIGEN_STRICT_LATENCY_TESTS=ON`: build + run; the strict path asserts a loose sanity ceiling (direct+event median ≤ 2 µs — a regression tripwire above the 1.5 µs `REQ_LOGC_006` WCET, not the accept bar). Verify: the gate passes on this machine.
- [x] 3.3 TSAN (`-DNEXIGEN_ENABLE_TSAN=ON`) and ASAN (`-DNEXIGEN_ENABLE_ASAN=ON`) gate-ON builds: full test suite passes with the benchmark printing only. Verify: no sanitizer reports.

## Workflow follow-up

- После получения цифр — отдельным change'ем принять решение «a» vs «b» и зафиксировать порог приёмки в спеке.
- Archive the change after the project's review requirements are satisfied.
