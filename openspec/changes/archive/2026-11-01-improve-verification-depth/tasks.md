# Tasks

## 1. Fuzzable HTTP parser

- [x] 1.1 Extract `url_decode`, header helpers, query parsing, and request-line validation into `include/nexigen/hub/detail/http_parse.hpp`. Verify: the hub suite passes with identical status codes.
- [x] 1.2 Add `tests/fuzz/http_parse_fuzz.cpp` and `tests/fuzz/corpus/http_parse/` seeds. Verify: the harness builds and fuzzes the pure helpers.
- [x] 1.3 Add `NEXIGEN_BUILD_FUZZERS` (Clang only) and a CI smoke run (`-runs=20000`). Verify: configure, build, and the fuzz command exit 0.

## 2. Fault injection

- [x] 2.1 Add `core::detail::fault_injection` and `NEXIGEN_ENABLE_FAULT_INJECTION` with compile-time no-op behavior when disabled. Verify: default builds contain no fault-branch overhead.
- [x] 2.2 Inject runner `pipe2`/`fork`/`/dev/null` and hub `socket`/`bind`/`epoll`/`eventfd` failures, with tests for each. Verify: Debug+option suites pass and the injected branches are covered.
- [x] 2.3 Enable fault injection in the Debug and coverage CI jobs. Verify: the YAML parses and the coverage report rises.

## 3. Analyzer depth

- [x] 3.1 Enable `clang-analyzer-optin.cplusplus.*` and `clang-analyzer-optin.portability.*`, keeping padding documented as disabled. Verify: the full `src/*.cpp` clang-tidy run exits 0.

## 4. Documentation

- [x] 4.1 Add P5 completion notes and update the coverage/verification text. Verify: documented numbers match the final report and the fuzz/fault-injection commands match CI.

## 5. Verification

- [x] 5.1 Build and run 100% of CTest under Debug (fault injection ON), Release, portable Release, TSan, and ASan.
- [x] 5.2 Build and run the Clang Debug suite under `-Werror`.
- [x] 5.3 Run the libFuzzer smoke command and clang-tidy over `src/*.cpp`.
- [x] 5.4 Run the coverage target with the 95% gate.
- [x] 5.5 Run `openspec validate improve-verification-depth --strict` and `openspec validate --specs`.

## Workflow follow-up

- Archive the change after all verification tasks pass.
- Re-run `openspec validate --specs` against the archived result.
