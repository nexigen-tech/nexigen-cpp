# Tasks

## 1. Table engine in core

- [x] 1.1 Add `FieldSpec` + `read_fields`/`write_fields` drivers to
      `include/nexigen/core/settings_io.hpp` (sink sub-object handled by the
      driver; exact diagnostic texts sourced from today's loaders).
- [x] 1.2 Extend `tests/core/core_test.cpp`: missing key, wrong type,
      out-of-range (`"expected an integer in range"`), sink-not-object
      (`"expected a JSON object"`) — one probe table covers all arms.

## 2. Migrate loaders (wire/diagnostic-identical)

- [x] 2.1 `src/chrono/settings.cpp` → field table.
- [x] 2.2 `src/metrics/metrics_settings.cpp` → field table.
- [x] 2.3 `src/tracing/tracing_settings.cpp` → field table.
- [x] 2.4 Evaluate `src/logging/settings.cpp` against the table; migrate only
      if it fits without distortion, otherwise record the decision.
      DECISION: not migrated — logging's loader is a sanitizer (rebuilds a
      `clean` document, bespoke per-field messages, LoggerOptions::from_json
      mapping), a different contract outside the clone cluster (design
      non-goal "no distorted fit"). Recorded here and in commit history.
- [x] 2.5 All existing settings tests pass without edits (they pin exact
      diagnostic strings and order).
      PROOF: full serial ctest with zero test-file edits; the chrono/metrics/
      tracing settings suites passed before and after every lift step.

## 3. Gates and verification

- [x] 3.1 Re-run the clone scan (`/tmp/opencode/clone_detect.py`) — the
      settings↔settings clone cluster must be gone.
      RESULT: zero settings↔settings pairs remain (was 336/314/349/245/245);
      this required lifting the loader skeleton and env overlay too (design
      D5-D7, scope note in proposal). Remaining top pairs belong to other
      clusters (enum glue from refactor-enum-traits-helper, composite/null
      sinks) — untouched here.
- [x] 3.2 Run clang-format/clang-tidy, naming and result-value guards, CCN gate.
      PROOF: clang-format clean on every touched file; clang-tidy clean on the
      three migrated TUs (CI scope src/*.cpp, clang build dir) — the tidy run
      exposed a latent clang POI issue from refactor-enum-traits-helper
      (explicit specialization after implicit instantiation), fixed by
      relocating the wrapped EnumTraits specializations between each enum and
      its glue (separate fix commit). Naming clean; result-value guard: single
      pre-existing violation (tests/tracing/record_format_test.cpp:185), out of
      scope. CCN gate: sole flagged item is CCN 17 in
      src/runner/child_process.cpp:84 (parallel session's untracked file);
      passes under the user-allowed threshold 20.
- [x] 3.3 Compile and pass 100% of unit tests under Debug, Release
      (`-O3 -march=native -flto` with latency gates), ThreadSanitizer
      (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer
      (`-DNEXIGEN_ENABLE_ASAN=ON`).
      PROOF: 37/37 Debug, 37/37 Release (strict latency), 37/37 ASAN,
      37/37 TSAN (all serial ctest); plus clang build 17/17 tests and clean
      clang-tidy on the migrated TUs.
