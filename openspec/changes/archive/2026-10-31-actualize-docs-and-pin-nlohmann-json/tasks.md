# Tasks

## 1. Pin nlohmann::json to v3.12.0

- [x] 1.1 Update the FetchContent URL in CMakeLists.txt from v3.11.3 to v3.12.0 and verify a fresh configure (with the system package absent and the FetchContent path taken) resolves and builds successfully.
- [x] 1.2 Update the README serialization-contract note ("FetchContent fallback (pinned v3.11.3)") to reference v3.12.0 and verify the text matches the CMakeLists pin.

## 2. Actualize documentation figures

- [x] 2.1 In docs/architecture.md, update "25 capabilities / 160 requirements / 387 scenarios" to the current counts (43 / 271 / 671) and verify the numbers match `openspec validate --specs` and a recount of the `openspec/specs/` corpus.
- [x] 2.2 Add the missing `nexigen::tracing` and `nexigen::diagnostics` rows to the architecture module-map table and verify the table now lists all seven modules (core, chrono, logging, metrics, tracing, diagnostics, app).

## 3. Verification

- [x] 3.1 Compile and pass 100% of unit tests under Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`), confirming the nlohmann::json version bump introduces no regressions.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived result.