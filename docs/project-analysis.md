# nexigen-cpp — project analysis

Date: 2026-11-01
Branch: `learning-enhancement` at `6f2bebf`

> **Status:** P0 items are being implemented in
> `openspec/changes/improve-quality-gates`. P1 and P2 remain open.

**Overall assessment: 8/10** — architecturally strong, well-specified project at
early-production quality. The main risks are process risks rather than runtime
defects: the coverage gate is broken, CI is absent, and documentation has drifted.

This analysis is read-only. It was produced by inspecting the repository and by
running the coverage build, a Clang build, clang-tidy, a header self-containment
check, and repeated test runs.

---

## 1. Metrics

| Metric | Value |
|---|---:|
| C++ code | 49 892 lines / 152 files |
| Production code | 28 392 lines |
| Tests | 18 111 lines / 11 files |
| Examples | 3 389 lines / 17 files |
| Specifications | 23 804 lines / 258 files |
| Capabilities | 58 |
| Requirements / scenarios | 407 / 1 096 |
| Archived changes | 31 |
| CTest tests | 33 (13 unit/CLI + 20 examples/help) |
| Commits | 63 over 9 days |

Non-blank C++ lines: 44 980. Production split: `include/` 15 896, `src/`
11 654, `tools/` 842.

---

## 2. Architecture

### Dependency graph

```
core (header-only)
 ├── chrono ──┬── logging
 │            ├── metrics
 │            ├── tracing
 │            └── diagnostics
 ├── app ← logging, metrics, tracing, chrono
 └── runner ← app
      └── hub ← runner
```

- No dependency cycles. `detail/` headers are not consumed across modules.
- Hot/cold separation holds: hot paths live in headers and use atomics without
  allocation; cold paths live in `src/*` and may take control-plane mutexes.
- `hub` depends on `runner` only for metadata structures; there is no reverse
  dependency.

### Strengths

- **89/89 public headers are self-contained** — each compiles alone with
  `-Wall -Wextra -Werror`.
- Every header uses `#pragma once`; there are no C-style casts and no
  `dynamic_cast`/`typeid`.
- `-fno-exceptions`, `-fno-rtti`, `JSON_NOEXCEPTION=1`, and `-Werror` are active
  for all targets.
- 177 `static_assert`s pin enum contracts and type sizes.
- No `malloc`/`free`, `strcpy`, `sprintf`, `system()`, or `popen`.
- 91 uses of `std::atomic`; 18 `std::mutex` uses, all on cold paths.
- `O_CLOEXEC` is used consistently for file descriptors.

---

## 3. Tests and coverage

### Runs

- Debug, Release, TSan, ASan: **33/33 tests**, 0 failed.
- Approximately **348 000 runtime checks** (metrics and tracing contribute
  tens of thousands through loops).
- Repeated runs of `nexigen_hub_tests` (5x), `nexigen_runner_tests`, and
  `nexigen_app_suite_tests` (3x) were stable.
- Release latency gates pass with margin:
  - logging: **0.50 ns** against a 20 ns gate;
  - tracing lifecycle: **10.0 ns** against a 20 ns gate;
  - metrics increment: **0.18 ns** single-thread, **4.39 ns** multi-thread;
  - diagnostics calibrated start+stop: **16.7 ns**.

### Actual coverage

The official report shows **95.9%**, but it **excludes `runner` and `hub`**:

| Module | Coverage |
|---|---:|
| core | 100.0% |
| diagnostics | 100.0% |
| chrono | 96.2% |
| metrics | 96.2% |
| app | 95.8% |
| tracing | 95.5% |
| logging | 95.3% |
| **runner** | **58.4–58.7%** |
| **hub** | **80.6–80.8%** |
| **Actual total** | **86.2–86.3%** (≈9 950 / 11 532) |

Weakest files:

- `src/runner/runner.cpp` — **39.0%**
- `src/runner/app_suite.cpp` — **55.7%**
- `src/hub/streaming.cpp` — **69.7%**
- `src/hub/http_server.cpp` — **80.2%**

The documented ≥95% target is therefore not met by the two newest subsystems.

---

## 4. Specifications and process

- 58 capabilities, 407 requirements, 1 096 scenarios.
- `openspec validate --specs` — **58 passed, 0 failed**.
- 31 archived changes with full planning history.
- Spec-to-code-to-test traceability is good: most changes carry verification
  criteria per task.
- The spec-driven process is the project's main strength.

---

## 5. Findings

### P0 — critical for confidence in quality

**1. The coverage gate is broken and misleading**

- `CMakeLists.txt:345-352` — the `coverage` target's `DEPENDS` omits
  `nexigen_runner_tests`, `nexigen_app_suite_tests`,
  `nexigen_runner_logging_tests`, and `nexigen_hub_tests`.
- On a clean configuration, `cmake --build build/coverage --target coverage`
  fails: six tests are "Not Run" and CLI tests cannot find their binaries.
- `cmake/coverage_report.py:80` — the module list omits `runner` and `hub`, so
  they are silently excluded from TOTAL.
- `README.md:57,61` and `docs/architecture.md:129,151` claim 96.1% and "seven
  tests" — both are stale.

**Recommendation:** add the four targets to `DEPENDS`; derive the module list
from the collected data; recompute the gate with the new modules; update the docs.

**2. CI is absent**

- `.github/`, `.gitlab-ci.yml`, and `.circleci` do not exist.
- README and architecture promise "enforced by CI", but no automated check runs.

**Recommendation:** add a `debug/release/tsan/asan/coverage` matrix plus
`openspec validate --specs` and clang-tidy.

### P1 — high priority

**3. Clang `-Werror` build fails**

Verified with Clang 21.1.8 (the project claims Clang 18+):

- `include/nexigen/metrics/meter.hpp:123` — unused parameter `key`;
- `include/nexigen/metrics/null_metric_sink.hpp:47` — unused parameter `sink`;
- `tests/metrics/metrics_test.cpp:360-370` — unused const variables;
- `examples/core/ex_spsc_ring_buffer.cpp:36` — unused function.

With three `-Wno-error=...` suppressions the build succeeds and all 33 tests
pass, so the issue is `-Werror` cleanliness rather than portability.

**4. Documentation has drifted**

- `docs/architecture.md:10-20` — the module map omits `runner` and `hub`.
- `docs/architecture.md:151` — 43 capabilities / 271 requirements / 671
  scenarios instead of 58 / 407 / 1 096.
- README has no sections for `tracing`, `runner`, or `hub`, although the modules
  and their docs exist.
- README says "All three modes" although there are four configurations.

**5. No install/export/package support**

- There are **0** `install(` calls.
- `cmake --install` is impossible; there is no `nexigenConfig.cmake`, version
  file, or export set.
- For an ecosystem backbone this blocks external consumption.

### P2 — medium priority

**6. Low runner/hub coverage** — see the table above. `runner.cpp` (39%) and
`app_suite.cpp` (56%) contain fork/exec, restart policies, and error handling.

**7. Network FetchContent without a hash**

`CMakeLists.txt` downloads nlohmann/json by URL with a pinned tag but no
`URL_HASH`. Offline builds are impossible and reproducibility depends on GitHub.

**8. `-march=native` in Release**

`cmake/Optimizations.cmake` makes binaries non-portable across CPUs. This is
acceptable for an HIL bench, but an explicit portable mode is needed for
distribution.

**9. Control-plane errors are swallowed**

`static_cast<void>(write_json_file(...))`, `static_cast<void>(discovery.rescan())`,
and `static_cast<void>(streaming.sync(...))` ignore state-write and refresh
failures. For an observability tool this is more dangerous than for a library.

**10. Fragile HTTP edges**

- `Last-Event-ID` is recognized only in two case variants
  (`src/hub/http_server.cpp`), although HTTP header names are case-insensitive.
- The parser targets minimal HTTP/1.1; there are no fuzz or property tests for
  malformed requests.

### P3 — suggestions

- `src/hub/streaming.hpp:21-24` and `src/hub/http_server.cpp:41-46` — unsigned
  multiplication before conversion to `size_t` (safe, but clang-tidy correctly
  flags it).
- `src/hub/discovery.cpp:457` — `push_back` in a loop without `reserve`.
- Long functions: `Runner::start` (~336 lines), `run_suite_supervisor` (~209),
  `handle_request` (~143) — split into phases.
- `detail/affinity.hpp` is duplicated in four modules — a documented decision
  that requires discipline.
- The `Result::value()` ban is convention-only; add a grep/clang-tidy check to CI.

---

## 6. Improvement plan

| Priority | Action | Expected effect |
|---|---|---|
| P0 | Fix the `coverage` target and module report | Honest ~86.3% instead of a fictitious 95.9% |
| P0 | Add a CI matrix | Regression protection |
| P1 | Fix the four Clang `-Werror` findings | The claimed Clang support becomes real |
| P1 | Refresh README and architecture.md | Documentation matches the code |
| P1 | Add install/export/package config | Usable by the ecosystem |
| P2 | Raise runner/hub coverage to ≥90% | Control over fork/exec and HTTP edges |
| P2 | Log rescan/sync/write failures | Observability reliability |
| P3 | Fuzz the HTTP parser, split long functions | Robustness and maintainability |

---

## 7. Verdict

The project shows **high engineering discipline**: strict invariants, real
sanitizers, latency gates, a spec-driven process, and self-contained headers.
It is no longer a prototype; it is a solid library foundation.

The main risk is **the illusion of full coverage**: the newest modules reduce
actual coverage substantially while the reporting tool hides them. The second
risk is **the absence of CI**, which leaves all current guarantees dependent on
manual runs. Fixing these two items should raise the assessment to 9/10.

## 8. P0 completion notes (2026-11-01)

P0 was implemented in `openspec/changes/archive/2026-11-01-improve-quality-gates`:

- The `coverage` target now builds all eleven unit-test binaries, excludes CLI
  smoke tests, and derives module rows from gcov data. The report shows
  **86.2–86.3%** overall, including `runner` and `hub`.
- CI runs Debug, Release, TSan, ASan, coverage, and OpenSpec jobs in
  `.github/workflows/ci.yml`. TSan uses `--security-opt seccomp=unconfined`
  because Docker's default profile blocks `personality(ADDR_NO_RANDOMIZE)`.
- Shared runners measure 80–100 ns on the logging enqueue benchmark, so CI
  Release sets `-DNEXIGEN_STRICT_LATENCY_TESTS=OFF`; the 20 ns gate remains the
  default for dedicated hardware.
- Tests now create their `/tmp/opencode` scratch root instead of assuming it
  exists, which makes the suites pass in a clean container.
- CI enforces the honest interim 85% coverage gate; raising runner/hub coverage
  to restore the 95% project target remains P2.

## 9. P1 completion notes (2026-11-01)

P1 was implemented in `openspec/changes/improve-distribution`:

- **Clang:** the four `-Werror` findings are fixed without suppressing warnings.
  CI runs a dedicated Clang job in the `gcc:15` container (Clang 19 + libstdc++ 15).
  The earlier Clang 18 attempt exposed a toolchain constraint, not a code defect:
  `std::expected` requires a C++23 standard library, and Ubuntu 24.04's
  libstdc++ 13 is too old. Documentation now states that requirement.
- **Install/export:** `cmake --install` writes `include/nexigen/`, eight static
  archives, both CLI tools, `nexigenTargets.cmake`, `nexigenConfig.cmake`, and a
  same-minor version file. Build-only flags stay behind `$<BUILD_INTERFACE>`.
  `find_dependency` resolves `Threads` and `nlohmann_json`; a standalone consumer
  in `tests/packaging/consumer/` verifies the installed package.
- **Docs:** README now documents `tracing`, `runner`, `hub`, Clang, and package
  consumption; the architecture page has the packaging section; the unused
  `io_uring` claim and the stale module list are gone.

P2 remains: runner/hub coverage, control-plane error logging, FetchContent hash,
portable Release mode, and HTTP edge hardening.

## 10. P2 completion notes (2026-11-01)

P2 was implemented in
`openspec/changes/archive/2026-11-01-improve-runtime-reliability`:

- **Coverage:** forked runner supervisors and pre-`exec` children flush gcov
  counters, so control-plane work is measured instead of discarded. The report
  now shows **94.2%** overall (10 971 / 11 645), with `runner` at **90.5%** and
  `hub` at **91.6%**. The `coverage` target enforces `runner >= 90` and
  `hub >= 90`; CI raises its overall gate to 90%.
- **Control-plane diagnostics:** `Http_server` and `Stream_manager` accept a
  function-pointer diagnostic sink. Discovery rescan failures, tailer
  open/stat/read failures, and `sync()` failures are reported; `nexigen-hub`
  forwards them to stderr. Runner JSON state-write failures are reported at the
  `write_json_file` choke point through `warn_runner`.
- **HTTP edges:** `Last-Event-ID` and `Connection` are parsed with a
  case-insensitive header helper that trims whitespace and cannot match a
  similarly named header. Malformed request lines, versions, oversized inputs,
  invalid `limit`/`since` values, invalid escapes, and non-GET methods have
  regression coverage.
- **Reproducible and portable builds:** the `nlohmann/json` archive is pinned
  with its SHA-256 digest. `-DNEXIGEN_NATIVE_ARCH=OFF` removes
  `-march=native`/`-mtune=native` while keeping `-O3` and LTO; CI builds and
  tests that mode.
- **Remaining:** the project-wide 95% target is within reach but not yet
  enforced; the P3 items (HTTP fuzzing, long-function splits, tidiness checks)
  remain open.

## 11. P3 completion notes (2026-11-01)

P3 was implemented in
`openspec/changes/archive/2026-11-01-improve-maintainability`:

- **Long functions:** `Runner::start` now composes `resolve_start_layout`,
  `prepare_start_layout`, `open_start_logs`, and `run_instance_supervisor`;
  the suite supervisor loop delegates to `finish_suite_stop`,
  `advance_suite_restart`, and `advance_app`; `Http_server::handle_request`
  dispatches to per-route handlers. The public API and fork/exit ordering are
  unchanged.
- **HTTP robustness:** a fixed-seed malformed-request corpus drives 120
  bounded byte sequences through the live server and verifies that every case
  either receives a bounded HTTP response or is closed, then checks a final
  health probe. The server now processes buffered data before closing a
  half-closed client.
- **Convention guard:** `cmake/check_result_value.py` strips comments/literals,
  extracts every `.value()` receiver, and fails on Result/Status receivers. It
  runs in the coverage CI job; known span-ID and nlohmann receivers are
  allowlisted.
- **Static nits:** constant byte counts are multiplied as `std::size_t`, and
  `Discovery::scan_standalone` reserves its instance vector.
- **Coverage after the refactor:** **94.2%** overall (11 037 / 11 713),
  `runner` **90.6%**, `hub` **91.8%**. All module gates still pass.
- **Remaining:** the project-wide 95% target was the only open quality item
  (closed by P4 below); coverage-guided fuzzing and clang-tidy integration were
  optional future work at that point.

## 12. P4 completion notes (2026-11-01)

P4 was implemented in
`openspec/changes/archive/2026-11-01-improve-maintainability-p4`:

- **Coverage target reached:** the overall line coverage is **95.1%**
  (11 191 / 11 753), with `runner` at **93.0%** and `hub` at **95.2%**.
  New tests cover streaming path changes and directory read failures, SSE gap
  replay/live delivery, percent-decoded stream IDs, discovery state derivation
  and standalone state branches, runner settings/stream preparation failures,
  suite metadata write failures, invalid logging settings, live-supervisor
  stops, and hostile metadata/status JSON.
- **CI gate:** the coverage job now enforces the **95%** project gate while
  keeping the 90% runner/hub module gates.
- **clang-tidy:** `.clang-tidy` enables `bugprone-*`, `performance-*`,
  `portability-*`, and non-opt-in `clang-analyzer-*` checks with warnings as
  errors. The Clang CI job runs the profile over every `src/*.cpp` translation
  unit. Finding fixes include enum sizes, cast ordering, structured pointer
  conversion, a vector reserve, optional access, and `string_view` data usage;
  noisy checks are disabled with documented rationale.
- **Remaining:** only optional future work (coverage-guided fuzzing, additional
  analyzer opt-ins, and long-tail platform fault injection).

## 13. P5 completion notes (2026-11-01)

P5 was implemented in
`openspec/changes/archive/2026-11-01-improve-verification-depth`:

- **Fuzzable parser:** the pure HTTP helpers now live in
  `include/nexigen/hub/detail/http_parse.hpp`; `Http_server` calls the same
  functions. A libFuzzer harness (`tests/fuzz/http_parse_fuzz.cpp`) checks
  request-line invariants, header-view bounds, and decode-size bounds against a
  four-file seed corpus.
- **Fuzz CI:** `NEXIGEN_BUILD_FUZZERS=ON` builds the Clang-only harness; the
  Clang job runs a 20 000-iteration smoke over the corpus.
- **Fault injection:** `NEXIGEN_ENABLE_FAULT_INJECTION=ON` enables test-only
  cold-path hooks for runner `pipe2`/`fork`/`/dev/null` and hub
  `socket`/`bind`/`epoll`/`eventfd`. Tests cover all seven points; the checks
  compile to constant `false` in production builds. Debug and coverage CI use
  the option.
- **Analyzer depth:** `clang-analyzer-optin.cplusplus.*` and
  `clang-analyzer-optin.portability.*` are enabled; the padding opt-in stays
  disabled with a documented alignment rationale.
- **Coverage:** **95.2%** overall (11 191 / 11 753), `runner` **93.0%**,
  `hub` **95.2%**. The 95% project gate and 90% module gates still pass.
- **Remaining:** no required quality work; platform-specific calibration and
  true I/O fault injection remain optional long-tail work.
