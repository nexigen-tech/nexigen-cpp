# Proposal

## Why

Session instrumentation (analysis of the working session, Nov 9) shows the
build/test matrix consumed 1 h 42 min of a 3 h 41 min session (46%). The four
major drivers: the matrix is re-run several times per change because
late-discovered gate failures (clang-tidy enum POI) invalidate completed
runs; gcc's LTO link stage is serial (-flto with no worker count), dominating
the 17-min Release rebuild; ctest runs 37 tests serially because
`ex_app_cli_settings` races under `-j`; and 80 public headers pull
nlohmann/json, so every dependent TU pays the JSON parse cost on every
rebuild.

## What Changes

- Fail-fast validation ladder in `scripts/validate.sh`: module-tier (affected
  module tests + naming/CCN/format gates) before the CI tier; clang-tidy runs
  BEFORE the expensive sanitizer configs so a header-order/POI failure costs
  seconds, not a 40-minute matrix.
- Parallel LTO for gcc Release/RelWithDebInfo: `-flto=jobserver`-equivalent
  worker count (`-flto=N`) added on both compile and link side in
  `cmake/Optimizations.cmake`; N derived from `ProcessorCount`.
- PCH for library targets (`cmake/Pch.cmake` + `include/nexigen/core/pch.hpp`):
  precompiled `nlohmann/json.hpp` plus common cold-path std headers, with the
  `-Wnull-dereference` suppression embedded so consumers keep compiling under
  `-Werror` unchanged. Applied via `target_precompile_headers` because lib and
  test TUs compile with identical defines (verified: `-DJSON_NOEXCEPTION=1`,
  `-D_GNU_SOURCE`, `-D_POSIX_C_SOURCE=200809L` on both sides).
- ctest `-j` unlock: `nexigen_add_example` gives every example test a unique
  `WORKING_DIRECTORY` (ctest working dirs today collide on cwd-relative state
  such as `./nexigen-example-settings`), plus per-test unique `NEXIGEN_RUN_DIR`
  so runner-owned shared roots cannot collide either. Sanitizer configs get a
  capped parallelism default.
- ccache adoption: CMake autodetects `ccache` and sets
  `CMAKE_CXX_COMPILER_LAUNCHER` (no-op when absent); `scripts/validate.sh`
  reports `ccache` hit stats at the end of a run.
- Timing instrumentation: every proposed build/test step prints wall time, so
  future sessions can compare against the recorded baselines.
- Latent-race fix (product, exposed by the ctest `-j` unlock — see design D7):
  `src/hub/streaming.cpp` seed-read now keeps `offset` synced with the fd
  cursor (dropping a stale fstat-size override) and dispatches pending
  too-long-line gaps at seed end. This corrects a missing SSE `gap` event the
  existing `http_sse_gaps` suite already asserts; it is a behavior *fix*, not
  a diagnostic/wire-format change.

**Not in scope:** switching Release to clang+ThinLTO (policy change, separate
experiment), deleting stale `build-*` directories (campaign of its own,
parallel-session risk), and any change to shipped code semantics — runtime
behavior of the product must stay byte-identical (vocabulary strings,
diagnostics, wire format) — except the D7 gap-dispatch defect, whose absence
the shipped test suite already treats as a failure.

## Capabilities

### New Capabilities

- `validation-loop`: the fast inner/outer validation loop (script, parallel
  LTO-backed rebuild pace, parallel ctest, PCH'd rebuild cost) — a process
  Engineering mandate, not a runtime behavior of the library.

### Modified Capabilities

(none — `skip_specs: true`, build-infrastructure change; delta specs omitted
by convention as in the prior `refactor-*` changes)

## Impact

- **Performance/latency (ns)/memory-allocation impact of the change:** strict
  run-time of shipped code is untouched: `-flto=jobserver` style parallel
  workers change link orchestration only; PCH is compile-time only and does
  not alter generated semantics (same preprocessor state per TU: verified
  identical defines, same diagnostic pragmas); the ctest parallelism hides
  behind the no-shared-state guarantee of unique per-test working dirs.
  Measure-verify targets: Release incremental rebuild 16.9 min → ~6-9 min;
  full ctest wall ≈ sum → max(config); no new allocations on hot paths
  (nothing ships).
- **Risk:** PCH must not drag the `-Wnull-dereference` suppression into TUs
  that relied on the tightly scoped pragma — the suppression stays embedded
  inside the PCH header exactly as today's TU-level pragma blocks.
  `-flto=N` value must match on compile and link sides (gcc consistency
  check).
