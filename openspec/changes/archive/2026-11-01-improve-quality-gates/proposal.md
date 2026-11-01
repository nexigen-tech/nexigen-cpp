# Proposal

## Why

The P0 findings from `docs/project-analysis.md` are process defects, not runtime
defects:

- The `coverage` target does not depend on the newest test binaries, so it fails
  on a clean build. Its report script also omits `runner` and `hub`, hiding the
  real total: **≈86.3%**, not the reported 95.9%.
- The repository has no CI, although `README.md` and `docs/architecture.md`
  promise that the four-mode matrix and coverage gate are enforced automatically.

Both problems undermine confidence in the otherwise strong test and sanitizer
setup. This change fixes the quality gates without changing production code or
runtime behaviour.

## What Changes

- **Coverage target:** add the four missing test targets to `DEPENDS`:
  `nexigen_runner_tests`, `nexigen_app_suite_tests`,
  `nexigen_runner_logging_tests`, `nexigen_hub_tests`.
- **Coverage report:** derive the module list from collected gcov data instead of
  a hard-coded list, so new modules can never be silently omitted. Keep stable
  ordering for known modules.
- **CI:** add `.github/workflows/ci.yml` with:
  - Debug, Release, TSan, and ASan build/test jobs;
  - a coverage job that runs the existing `coverage` target with an honest
    interim gate;
  - an OpenSpec validation job.
- **Docs:** update the coverage section in `README.md` and
  `docs/architecture.md` with measured per-module numbers, the interim gate, and
  the CI entry point.

No production source, public API, compiler flags, or runtime behaviour changes.
`.openspec.yaml` uses `skip_specs: true` because this is a test/CI/documentation
change with no observable capability delta.

## Capabilities

### New Capabilities

None — `skip_specs: true`.

### Modified Capabilities

None — no observable behaviour changes.

## Impact

- **Code:** `CMakeLists.txt`, `cmake/coverage_report.py`,
  `.github/workflows/ci.yml`, `tests/support/test_temp.hpp`, the file-system test
  TUs, both CLI test scripts, `README.md`, `docs/architecture.md`, and
  `docs/project-analysis.md`.
- **Performance / latency / memory:** none. Production binaries and hot paths are
  untouched; only test target dependencies and reporting change.
- **CI runtime:** six jobs (four build modes, coverage, OpenSpec). Each build uses
  the existing CTest suite and timeouts.
- **Coverage gate:** the CI gate starts at the measured honest baseline and is
  raised as runner/hub tests improve in a later change.
