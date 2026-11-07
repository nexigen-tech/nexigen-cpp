# Proposal

## Why

After refactoring all production and test functions under CCN ≤ 15,
a CI gate is needed to prevent regression. The project now has zero
real C++ functions above the threshold; a lint-level check in CI
catches any future increase before a PR merges.

## What Changes

- Introduce a CMake custom target `complexity-check` that runs lizard
  on `include/`, `src/`, `tests/` with threshold 15, filtering out
  `alignas` false positives and excluding `examples/`.
- Add a standalone `complexity` job to `.github/workflows/ci.yml`
  that installs lizard and runs the check.

## Capabilities

### New Capabilities

None. CI-only quality gate.

### Modified Capabilities

None. The change sets `skip_specs: true`.

## Impact

- **Code**: `cmake/Complexity.cmake`, `cmake/RunComplexityCheck.cmake`,
  `cmake/check_complexity.py`, `CMakeLists.txt` (one include line),
  `.github/workflows/ci.yml` (new job).
- **Public API / performance / allocations**: none.
- **CI time**: ~10 extra seconds.
