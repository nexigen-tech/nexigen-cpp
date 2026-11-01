# Tasks

## 1. Fix the coverage target

- [x] 1.1 Add `nexigen_runner_tests`, `nexigen_app_suite_tests`,
  `nexigen_runner_logging_tests`, and `nexigen_hub_tests` to the `coverage`
  target's `DEPENDS` in `CMakeLists.txt`. Verify: a clean
  `cmake --build build/coverage --target coverage` builds every test binary and
  all eleven unit-test suites run.
- [x] 1.2 Replace the hard-coded module list in `cmake/coverage_report.py` with
  preferred-order plus discovered modules, and include any new module in TOTAL.
  Verify: the report prints `runner` and `hub` and reports ≈9 950 / 11 532
  (86.2–86.3%) on the baseline.
- [x] 1.3 Run the coverage target with
  `-DNEXIGEN_COVERAGE_FAIL_UNDER=85`. Verify: the command succeeds and prints the
  full per-module table.

## 2. Add CI

- [x] 2.1 Create `.github/workflows/ci.yml` with Debug, Release, TSan, and ASan
  jobs using the `gcc:15` container, installing `cmake`, `ninja-build`, and
  `python3`. Verify: YAML parses and each job configures, builds, and runs
  `ctest --output-on-failure`.
- [x] 2.2 Add a coverage job using
  `-DNEXIGEN_ENABLE_COVERAGE=ON -DNEXIGEN_COVERAGE_FAIL_UNDER=85` and the
  `coverage` target. Verify: the job uses the fixed target and gate.
- [x] 2.3 Add an OpenSpec job on `ubuntu-24.04` with Node.js 22 and
  `@fission-ai/openspec@1.14.3` that runs `openspec validate --specs` and
  `openspec validate --changes --strict`. Verify: both commands exit zero.
- [x] 2.4 Make the test scratch root self-creating:
  `tests/support/test_temp.hpp` plus includes in the file-system test TUs, and
  `mkdir -p "${NEXIGEN_TEST_TMPDIR:-/tmp/opencode}"` in both CLI scripts.
  Verify: Debug tests pass in a fresh `gcc:15` container with no pre-created
  `/tmp/opencode`.
- [x] 2.5 Run the TSan matrix entry with
  `--security-opt seccomp=unconfined` and document why. Verify: TSan reports
  100% tests passed instead of the intermittent
  `personality(ADDR_NO_RANDOMIZE)` CHECK failure.
- [x] 2.6 Add `NEXIGEN_STRICT_LATENCY_TESTS` (default `ON`) and configure the
  CI Release job with `-DNEXIGEN_STRICT_LATENCY_TESTS=OFF`. Verify: Release CI
  passes on a shared runner while the default local Release still enforces the
  20 ns gates.

## 3. Documentation

- [x] 3.1 Update the coverage section in `README.md`: correct the test-binary
  count, state the honest ≈86.3% total, the 95.9% legacy-module figure, runner/hub
  numbers, and the interim 85% CI gate.
- [x] 3.2 Update `docs/architecture.md`: add `runner` and `hub` to the module
  map, refresh capability/requirement/scenario counts to 58 / 407 / 1 096, and
  describe the CI matrix and coverage gate.
- [x] 3.3 Link `docs/project-analysis.md` from `README.md` or
  `docs/architecture.md` so the P1/P2 backlog is discoverable.

## 4. Verification

- [x] 4.1 Run Debug, Release, TSan, and ASan builds and
  `ctest --output-on-failure`. Verify: 100% tests pass in all four modes.
- [x] 4.2 Run `openspec validate improve-quality-gates --strict`. Verify: the
  change validates.
- [x] 4.3 Run `openspec validate --specs`. Verify: 58 specs pass.

## Workflow follow-up

- [x] 5.1 Archive the change after CI, coverage, and docs verification, then
  continue with P1 (Clang warnings, documentation refresh, install/export).
