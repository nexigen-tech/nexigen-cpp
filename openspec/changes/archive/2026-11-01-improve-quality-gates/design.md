# Design

## Context

The analysis in `docs/project-analysis.md` established the baseline on GCC
15.2, Debug, with all current test binaries built:

| Scope | Covered / total | Coverage |
|---|---:|---:|
| core, chrono, logging, metrics, tracing, diagnostics, app | 7 327 / 7 639 | 95.9% |
| runner | 1 388 / 2 363 | 58.7% |
| hub | 1 237 / 1 530 | 80.8% |
| **All modules** | **≈9 950 / 11 532** | **86.2–86.3%** |

The `coverage` target was configured for the original seven modules and was not
updated when `runner` and `hub` landed.

## Goals / Non-Goals

**Goals:**

- A clean `coverage` build succeeds without manually building targets first.
- The coverage report includes every module that contributes instrumented
  `include/` or `src/` code.
- CI runs the four-mode matrix, the coverage target, and OpenSpec validation.
- Documentation states the measured numbers and the actual gate.

**Non-Goals:**

- Raising runner/hub coverage to 95% (tracked as P2 in the improvement plan).
- Fixing the Clang `-Werror` findings (P1).
- Changing production code, compiler flags, or sanitizer configuration.
- Publishing install/export rules (P1).

## Decisions

### D1 — Keep the coverage target unit-test-only

The target runs `ctest -R "^nexigen_.*_tests$"`, so CLI smoke tests remain
outside coverage. CLI tests exercise subprocesses and filesystem layouts; their
binaries are not needed by the coverage target. `DEPENDS` therefore lists only
the eleven unit-test binaries, matching the CTest regex.

### D2 — Derive module names from gcov data

`cmake/coverage_report.py` currently hard-codes seven module names. Replace that
with a preferred-order list plus any discovered module keys, and append `other`
only when present. This makes new modules visible without another code change and
keeps deterministic table ordering.

### D3 — CI uses a GCC 15 container

The project target is Ubuntu 26.04 / GCC 15.2+. GitHub-hosted `ubuntu-24.04`
ships an older GCC. The build and coverage jobs run in the official `gcc:15`
container and install `cmake`, `ninja-build`, and `python3`; this matches the
developer toolchain and keeps gcov aligned with the compiler.

The OpenSpec job runs on `ubuntu-24.04` because it only needs Node.js and the
pinned `@fission-ai/openspec@1.14.3` CLI.

TSan calls `personality(ADDR_NO_RANDOMIZE)`, which Docker's default seccomp
profile blocks. The TSan matrix entry runs its container with
`--security-opt seccomp=unconfined`; without it, TSan fails intermittently with
`tsan_platform_linux.cpp:290`. ASan and the non-sanitizer jobs keep the default
profile.

### D4 — Interim coverage gate

CI passes `-DNEXIGEN_COVERAGE_FAIL_UNDER=85` for now. The honest baseline is
≈86.3%, so the gate protects against regression without hiding the gap. The
project target remains ≥95%; the path from ≈86.3% to 95% is the P2 work item for
runner/hub tests.

### D5 — Workflow triggers and concurrency

Run on pushes to the main development branches and on every pull request.
`concurrency` cancels superseded runs on the same ref. `fail-fast: false` keeps
all four build-mode results visible.

### D6 — Latency gates are opt-out for shared CI

Release and RelWithDebInfo enable the ns-class gates by default. A container on
a shared runner measures 80–100 ns for the logging enqueue path instead of the
0.5 ns seen on the dedicated host, so the 20 ns gate fails even though the code
is unchanged. Add `NEXIGEN_STRICT_LATENCY_TESTS` (default `ON`); CI's Release
job sets it `OFF`. Optimized code, LTO, and all correctness checks still run in
CI. The gate remains the default for local and dedicated-hardware releases.

### D7 — Tests create their scratch root

The test suites use `/tmp/opencode` as a shared scratch namespace with
pid-unique names. Previously they assumed that directory already existed; a
clean container or CI runner does not have it, so file-system tests fail with
`NX_CHECK` errors that look like production bugs. Add
`tests/support/test_temp.hpp`, whose static initializer creates the directory,
and include it from every file-system test TU. The two CLI shell scripts create
the same directory (honoring `NEXIGEN_TEST_TMPDIR`) before calling `mktemp`.

## Risks / Trade-offs

- **Container availability:** `gcc:15` is a moving major tag. If the tag is not
  available, pin `gcc:15.2` in the workflow.
- **CI latency:** four full builds plus coverage. CTest timeouts already allow
  slow sanitizer runs; no test parallelism (ctest default) is used to keep
  latency gates meaningful.
- **Coverage gate honesty:** 85% is lower than the project aspiration. The
  workflow name and docs explicitly call it interim so the gap cannot be
  mistaken for completion.
