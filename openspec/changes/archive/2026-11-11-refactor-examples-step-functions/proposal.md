# Proposal

## Why

Seven of the twenty runnable examples exceed the project's cyclomatic-complexity target of 15, and the examples are the first code a user reads:

| Example | CCN | `main` NLOC |
|---|---:|---:|
| `app/ex_app_cli_settings.cpp` | 34 | 235 |
| `suite/ex_suite_app.cpp` | 36 | 159 |
| `metrics/ex_metric_provider_and_sinks.cpp` | 38 | 132 |
| `metrics/ex_counters_and_gauges.cpp` | 31 | 132 |
| `metrics/ex_descriptive_statistics.cpp` | 21 | 127 |
| `core/ex_spsc_ring_buffer.cpp` | 21 | 98 |
| `metrics/ex_histograms.cpp` | 16 | 86 |

The complexity gate deliberately excludes `examples/`, so this debt never surfaces in CI and grows silently. The long `main`s also mix setup, emission, snapshot and printing in one linear block, which makes the demonstrated API harder to follow than the library code itself.

Two of the audit's other findings do not hold on inspection: `examples/CMakeLists.txt` already provides `nexigen_add_example(name source target...)` with per-example CTest coverage, and the nine per-module `CMakeLists.txt` files are already thin lists of those calls. Nothing is duplicated there, and collapsing them would only move lines.

## What Changes

- Split each of the seven over-threshold examples into numbered step functions (`step_*`) with a short comment per step, keeping `main` as a readable sequence (open → emit → read back → report) and leaving every demonstrated API call, message and assertion untouched.
- Keep every example a single self-contained translation unit. A shared `example_common.hpp` was considered and rejected: copy-paste into a user's project is the main use, and a shared header would make each file non-runnable standalone.
- No change to `examples/CMakeLists.txt`, to the per-module `CMakeLists.txt` files, to output text, to exit codes or to the CTest registration.

## Capabilities

No capability deltas: the examples keep their documented behavior and output, so `.openspec.yaml` sets `skip_specs: true`.

## Impact

- **Code:** the seven example translation units only.
- **Behavior:** none. Each example's stdout, self-check assertions and exit code must stay identical; the before/after `ctest -L examples -V` transcripts are compared for that.
- **Build / coverage:** unchanged target list and test names.
- **Performance:** none; the same work in the same order, only separated into functions.
- **Out of scope:** the `#include` ordering, the per-module CMake layout, and any new example.
