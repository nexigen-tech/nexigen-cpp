# Design

## Context

See proposal.md — Why. Verified facts (measured or read from the tree):

- Machine: 64 cores, 122 GiB RAM, Ninja generator, `ld.lld` present, **no
  ccache/sccache/mold installed** (miniconda exists → ccache installable
  without root: `conda install -c conda-forge ccache`).
- Baselines from the Nov 9 session: Release full incremental rebuild (25
  headers touched) 16.9 min; ASAN configure+build 14.7 min; TSAN 12.2 min;
  ctest serial ×37 × ≥4 configs; matrix re-runs ×3-4 per change.
- LTO is enabled via `CMAKE_INTERPROCEDURAL_OPTIMIZATION` for Release/
  RelWithDebInfo only (`cmake/Optimizations.cmake`); sanitizer configs build
  Debug-like code, so LTO flags must stay Release-scoped.
- `ex_app_cli_settings` hard-codes `./nexigen-example-settings` relative to
  its test working directory; `nexigen_add_example` sets no
  `WORKING_DIRECTORY`, so all examples in a module share one dir under ctest;
  the runner additionally has env-driven roots (`telemetry_layout.hpp` reads
  `NEXIGEN_RUN_DIR`, …). Unit tests and lib TUs compile with identical
  defines and identical flag sets → one PCH cluster is safe.
- 80 public headers include `<nlohmann/json.hpp>` under a
  `-Wnull-dereference` pragma; the pragma must survive inside the PCH.

## Goals / Non-Goals

**Goals:**
- One validation entrypoint with fail-fast ordering and wall timing.
- Parallel LTO link for gcc (the single largest wall-time sink).
- PCH to amortize nlohmann/std parse cost across dependents.
- ctest `-j` unlocked without changing test semantics.
- ccache wired in when available (zero cost when absent).

**Non-Goals:**
- Compiler/linker swap for Release (clang ThinLTO experiment stays out).
- Deleting stale build dirs.
- Any shipped-code behavior change; wire format and diagnostics byte-identical.
- Reworking the CCN/complexity thresholding (already user-approved at 20).

## Decisions

1. **D1 — LTO worker count via `nexigen_options` INTERFACE.** In
   `nexigen_set_optimizations`, when IPO is enabled and the compiler is GNU,
   publish, only for Release/RelWithDebInfo configs, `-flto=<N>` on both
   `target_compile_options` and `target_link_options` of `nexigen_options`
   (N = `ProcessorCount`, capped at 32). All targets created after inherit
   it, matching how sanitizers/options already ride this umbrella. Clang
   keeps default LTO config unaffected (its `-flto` already exists).
   Consistency guard: same value on compile and link; a configure-time
   `message(STATUS)` prints the chosen N.
2. **D2 — single PCH header under core, one REUSE cluster.**
   `include/nexigen/core/pch.hpp` embeds: the GCC-only
   `#pragma GCC diagnostic push/ignored "-Wnull-dereference"` wrap, then
   `<nlohmann/json.hpp>`, then common cold-path std headers
   (`<string> <vector> <array> <memory> <chrono> <cstdint> <optional> <span>`).
   A helper `nexigen_precompile(target)` in `cmake/Pch.cmake` calls
   `target_precompile_headers(target PRIVATE <core/pch.hpp>)` and is applied
   to the nine `nexigen_<module>` STATIC libs and (via the same helper) the
   test executables and example hosts. No REUSE_FROM across clusters:
   lib/test/example link lines and defines are equal today, but keeping
   per-target PCH costs ~1 s each and is robust against future flag drift.
   Header is compiler-guarded (`__clang__` keeps the pragma from leaking
   warnings-as-errors on non-gcc builds).
3. **D3 — validate.sh tiers.**
   `scripts/validate.sh [--tier module|ci] [--modules m1,m2]`:
   - module tier: `cmake --build build-verify --target nexigen_<m>_tests…`
     + `ctest -R` affected + naming/CCN/format/complexity checks (~2 min).
   - ci tier: gates (format dry-run, naming, CCN-20-by-user-allowance,
     result-value, **clang-tidy on changed TUs BEFORE sanitizers**), then
     Debug ctest `-j` …, then build+ctest for Release, ASAN, TSAN **in
     parallel background jobs** (disjoint build dirs; waits; /usr/bin/time
     on every step; ccache stats summary).
   - Serial fallback flag `--serial` reproduces today's behavior.
4. **D4 — parallel ctest unlock.** In `nexigen_add_example`:
   `file(MAKE_DIRECTORY ...) unique `${CMAKE_BINARY_DIR}/examples/run/<name>`
   + `set_tests_properties(... WORKING_DIRECTORY <that dir> ENVIRONMENT
   "NEXIGEN_RUN_DIR=<that dir>/nexigen-run")` — cwd-relative collisions and
   shared runner roots both neutralized without touching example sources.
   ctest default parallelism stays conservative (`-j16`) even with 64 cores;
   TSAN same cap (RAM per TSAN test is the constraint, not cores).
   Completion addendum (same decision, source-side): the logging/chrono
   examples additionally hard-code **absolute** `/tmp/nexigen-examples-logs*`
   roots and `remove_all()` them at startup, which the cwd isolation cannot
   reach. Under `-j16` (ex_file_logger/ex_composite_logger share a prefix)
   and across the 3 parallel sanitizer configs one process deletes another's
   files mid-write (`flush failed: SystemError`). Those five examples now
   suffix their scratch roots with `::getpid()` (the pattern
   ex_metric_provider_and_sinks / ex_perfetto already used).
5. **D5 — ccache adoption.** Top-level `find_program(CCACHE ccache)` →
   `CMAKE_CXX_COMPILER_LAUNCHER` + `CMAKE_C_COMPILER_LAUNCHER`; prints a
   status line at configure. ccache is *not* vendored: install step is
   operator concern (`conda install -c conda-forge ccache`); everything
   degrades gracefully without it.
   Measured addendum: with invisibly-consumed `.gch` PCH files, ccache 4.14.1
   classifies 100% of compilations as *Uncacheable* until
   `sloppiness = pch_defines, time_macros` is set (persisted via
   `ccache --set-config` → `~/.config/ccache/ccache.conf`). Verified
   locally: same compile goes 1/1 Uncacheable → 1/1 Cacheable; no
   `-fpch-preprocess` flag needed. Documented posture: the sloppiness is
   safe here because no TU embeds `__TIME__/__DATE__`-dependent state and
   PCH define drift cannot happen (single fixed prelude compiled per target
   with target-identical defines).
6. **D6 — measurement discipline.** Every build/ctest step in validate.sh
   runs under `/usr/bin/time -f "… %es"`; tasks.md records before/after for
   the Release rebuild and ctest wall. Acceptance threshold: same 37/37
   results and no diagnostic drift (serial vs parallel ctest — randomized
   order shows shared-state bugs if any leak).
7. **D7 — latent-race fix discovered by the soak (product, in-scope).**
   Unlocking `ctest -j16` surfaced a pre-existing flake in
   `nexigen_hub_tests` (`[http_sse_gaps]`, ~5–10% even serially). Root cause
   in `src/hub/streaming.cpp` `open_tailer` seed-read: (a) it ended with
   `tailer.offset = status.st_size`, a fstat size captured *before* the
   read — a truncate→rewrite race lets the file grow mid-seed, desyncing
   `offset` from the fd cursor and wedging the tailer (every later read
   returns 0); (b) the seed-read path never dispatched `tailer.gaps`, so a
   too-long-line gap created while seeding was silently dropped (the
   `http_sse_gaps` assertion waits for exactly that event). Fix: drop the
   stale override (offset now tracks the fd cursor) and flush pending gaps
   at the end of the seed read, mirroring `poll_tailer`. Verified 30/30
   serial + ci-tier green. This is a wire-behavior correction the existing
   suite already asserts (a missing event that must arrive), not a
   diagnostic/format change.

## Risks / Trade-offs

- PCH can mask a TU that used to include `${nlohmann}` only transitively —
  intentional: vocabulary mandate makes json present in TUs anyway; PCH
  cannot add symbols (header-only).
- `-flto=32` when a matrix runs 4 builds in parallel could oversubscribe at
  link peaks; validate.sh serializes the LTO-heavy Release link by ordering
  (sanitizer builds are non-LTO, so runs don't collide).
- ctest `-j` could expose another latent race → tasks include a soak run
  (fixed test list repeated twice) before declaring the unlock done.

## Open Questions

- None. Threshold CCN 20 and `skip_specs` pre-confirmed by the user.
