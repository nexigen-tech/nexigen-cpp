# Tasks

## 1. Cross-module example

- [x] 1.1 Extend `nexigen_add_example(name source target)` in `examples/CMakeLists.txt` to forward extra link targets via `${ARGN}` (design D2). Verify: `cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug` configures and every existing example target still builds unchanged.
- [x] 1.2 Implement `examples/diagnostics/ex_stopwatch_histogram.cpp` per spec `diagnostics/examples` "ex_stopwatch_histogram records measured scopes into a histogram" / "renders the histogram on the console" (design D3–D5): calibrated `Stamp_source`, light/heavy `ScopedStopwatch` probes, geometric strictly increasing bounds, a `ScopedStopwatch` sink recording each tick into a provider-created `ExplicitBucketHistogram`, a whole-run `Stopwatch`, console summary/percentile/bucket-bar report, `HistogramSnapshot::to_string()`, and self-verification with non-zero exit on mismatch. Verify: the target builds with `-Werror` and running it directly prints the report and exits 0.
- [x] 1.3 Register `ex_stopwatch_histogram` in `examples/diagnostics/CMakeLists.txt` against `nexigen::diagnostics` and `nexigen::metrics` (design D1/D2). Verify: `ctest --test-dir build/debug -R ex_stopwatch_histogram --output-on-failure` passes and the test carries the `examples` label.

## 2. Documentation

- [x] 2.1 Add the `ex_stopwatch_histogram` run command and catalogue bullet to the README `## 🧪 Runnable Examples` section, and correct the introductory standard claim from C++20 to C++23 (design D6). Verify: the documented command runs as written from the build tree and exits 0; no `C++20` mention remains in `README.md`.
- [x] 2.2 Confirm the delta specs match the implemented behavior (`specs/examples` catalogue now twelve executables, `specs/diagnostics/examples` new requirement set). Verify: `openspec validate "add-stopwatch-histogram-example" --type change --strict` passes with no findings.

## 3. Integration gate

- [x] 3.1 Compile and run the example in the full build-mode matrix, each in its own build directory: Debug; Release (`-O3 -march=native` + LTO); ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`); AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify: `ctest --test-dir <dir> -R ex_stopwatch_histogram --output-on-failure` reports `100% tests passed` in all four modes and no sanitizer diagnostics are emitted.
- [x] 3.2 Validate catalogue coherence before archive: the README, `examples/` tree, CTest labels, and the change's `specs/examples` delta all name the same twelve executables (the main `openspec/specs/examples/spec.md` receives the twelfth name when the change is archived — see Workflow follow-up). Verify: a name-by-name cross-check shows no missing or extra executable and `openspec validate --specs` passes; `--strict` reports only the two pre-existing long-requirement warnings (`core/result`, `examples`) that predate this change.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied (`openspec archive add-stopwatch-histogram-example`), which applies the `examples` catalogue update and adds the `diagnostics/examples` requirement to the main specs.
- Verify the archived result: `openspec/specs/examples/spec.md` names twelve executables, `openspec/specs/diagnostics/examples/spec.md` contains the `ex_stopwatch_histogram` requirements, and the change directory moves under `openspec/changes/archive/`.
