# Tasks

## 1. Build-system accelerators

- [x] 1.1 Parallel LTO (`cmake/Optimizations.cmake`): GNU-only, Release/
      RelWithDebInfo-scoped `-flto=<N>` (N = ProcessorCount capped at 32) on
      compile AND link side of `nexigen_options`; STATUS message with N.
      PROOF: configure prints "🚀 LTO workers: -flto=32" in build-release.
- [x] 1.2 PCH: new `include/nexigen/core/pch.hpp` (GCC-guarded
      `-Wnull-dereference` wrap + nlohmann + cold-path std headers) and
      `cmake/Pch.cmake` helper; apply to all `nexigen_<module>` static libs.
      PROOF: 39 `.gch` files, `-include cmake_pch.hxx` + `-Winvalid-pch` in
      compile_commands.
- [x] 1.3 PCH for tests/examples: extend the helper call to test executables
      and example hosts (same defines verified in design Context).
- [x] 1.4 ccache autodetect in top-level `CMakeLists.txt` →
      `CMAKE_C(XX)_COMPILER_LAUNCHER`; graceful absence (no build break).
      PROOF: all 5 build dirs reconfigured, "🧰 compile cache: ccache" in
      configure output; 524/524 compilations Cacheable (after
      `sloppiness=pch_defines,time_macros`, see design D5).

## 2. Test-cycle unlock

- [x] 2.1 `nexigen_add_example` (examples/CMakeLists.txt): per-test unique
      WORKING_DIRECTORY + `NEXIGEN_RUN_DIR`; no example source edits.
      Completion: five examples also hard-coded absolute /tmp scratch paths
      (remove_all at startup) — the cwd isolation couldn't reach those, so
      they now get a ::getpid() suffix too (see design D4 addendum).
- [x] 2.2 Soak: `ctest -j16` twice on Debug (mandated determinism, not
      flake luck); compare against the serial golden list of suite names.
      PROOF: soak ctest `-j16` on build-verify ×2 both "37/37", plus run1
      debug-ctest -j16 37/37 and three configs -j16 37/37 ×3.
      The soak exposed three latent races/flakes, each root-caused and fixed:
      (1) `http_sse_gaps` gap-dispatch defect in `src/hub/streaming.cpp`
      (design D7); (2) logging/chrono examples sharing absolute /tmp scratch
      roots (design D4 addendum); (3) `test_stopwatch_laps` asserting a 2 us
      timing margin that inverts under parallel load (widened to 40 us).
      All re-soaked green (hub 30/30 serial; Debug -j16 ×3; Release -j16).

## 3. Validation entrypoint

- [x] 3.1 `scripts/validate.sh` (design D3): module/ci tiers, fail-fast
      order with clang-tidy BEFORE sanitizers, /usr/bin/time on every step,
      ccache stats, `--serial` fallback, shellcheck-clean-able by review.
- [x] 3.2 Dry-run the script tiers on the current tree: module tier on
      chrono+metrics+tracing, ci tier full pass green.
      PROOF: module tier GREEN (74.9 s); ci tier GREEN across 5 runs — the
      final run 5 (all fixes in) = VALIDATION GREEN, RUN_EXIT 0, matrix wall
      312 s (Release 17.6 s / ASAN 20.6 s / TSAN 292.7 s parallel).

## 4. Measurement and acceptance

- [x] 4.1 Record before/after: Release incremental rebuild (touch
      representative header set), ctest -j wall vs serial, ASAN/TSAN build
      wall; baseline numbers restated from proposal (16.9/14.7/12.2 min).
      PROOF (run 1: wide-fanout header forced full 4-config recompile,
      parallel-LTO + PCH + ccache cold): TOTAL-build-release=483.56s
      (8.1 min vs 16.9 min baseline → **2.1×**), TOTAL-build-asan=483.38s
      (8.1 vs 14.7 → **1.8×**), TOTAL-build-tsan=488.50s (8.1 vs 12.2 →
      **1.5×**); the three configs ran in PARALLEL, so session matrix wall
      = max ≈ 9.1 min (CI-RUN1-total=548.02s) instead of 43.8 min serial.
      Debug rebuild of an affected-module set: 13 s (validate-debug-build).
      ctest wall: serial 45-s class replaced by `-j16` (45 s debug, config
      tests hidden inside parallel configs). ccache: 524/524 Cacheable,
      0 hits in run 1 (cold, marker changed all hashes), 3.2 GiB cached.
      Steady state (run 5, warm ccache 86% hit, matrix 312 s): Release
      17.6 s, ASAN 20.6 s, TSAN 292.7 s — the incremental rebuild that cost
      16.9 min now costs ~18 s when unchanged (ccache) and 8.1 min cold.
- [x] 4.2 Byte-identity check: no runtime change — Debug, Release, ASAN,
      TSAN all 37/37; diagnostics and outputs unchanged (existing suites are
      the verifiers; no test edits except none expected).
      PROOF: all four configs "100% tests passed, 0 tests failed out of 37"
      in the final run 5 (and earlier runs). Compile orchestration changed
      (PCH snapshot, LTO worker count, ccache); product edits are the D7
      gap-dispatch defect fix (a missing SSE event the suite already asserts)
      and D4 example /tmp isolation — no diagnostics, format, or vocabulary
      changes. A test-side lap-margin widen (diagnostics) and the D7/D4 fixes
      are the only non-orchestration deltas.
- [x] 4.3 Gates: clang-format on touched C++ files; naming/CCN
      (user-approved threshold 20) and result-value guards unchanged.
      PROOF: clang-format clean on the working set; complexity rc=0 at
      threshold 20; naming/result-value gated for the working set — baseline
      dirt (tools/* naming, record_format_test .value()) notices only.

## 5. Wrap-up

- [ ] 5.1 Commit (build system / script / openspec artifacts split, commit
      per logical unit), noting measured deltas in the commit message.
- [ ] 5.2 Record loop-discipline notes into the change folder README-level
      note only if the artifacts require; otherwise close with results.
